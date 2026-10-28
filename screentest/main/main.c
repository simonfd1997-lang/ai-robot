// Karen screen test: ST7789 240x240, no CS. CLK=19 MOSI=20 DC=47
// Cycles SPI modes: MODE 0 -> RED, MODE 3 -> GREEN, MODE 2 -> BLUE, MODE 1 -> WHITE
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

#define TAG "SCREENTEST"
#define PIN_CLK 19
#define PIN_MOSI 20
#define PIN_DC 47
#define W 240
#define H 240
#define LINES 20

static SemaphoreHandle_t done;
static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(done, &hp);
    return hp == pdTRUE;
}

static uint16_t *buf;

static void fill(esp_lcd_panel_handle_t p, uint16_t color) {
    uint16_t c = (color >> 8) | (color << 8);   // byte swap for SPI
    for (int i = 0; i < W * LINES; i++) buf[i] = c;
    for (int y = 0; y < H; y += LINES) {
        esp_lcd_panel_draw_bitmap(p, 0, y, W, y + LINES, buf);
        xSemaphoreTake(done, pdMS_TO_TICKS(500));
    }
}

static void stripes(esp_lcd_panel_handle_t p) {
    const uint16_t cols[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    for (int y = 0; y < H; y += LINES) {
        uint16_t c = cols[(y / LINES) % 4];
        c = (c >> 8) | (c << 8);
        for (int i = 0; i < W * LINES; i++) buf[i] = c;
        esp_lcd_panel_draw_bitmap(p, 0, y, W, y + LINES, buf);
        xSemaphoreTake(done, pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    done = xSemaphoreCreateBinary();
    buf = heap_caps_malloc(W * LINES * 2, MALLOC_CAP_DMA);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = W * LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    const int modes[4] = {0, 3, 2, 1};
    const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    const char *names[4] = {"RED", "GREEN", "BLUE", "WHITE"};

    while (1) {
        for (int m = 0; m < 4; m++) {
            esp_lcd_panel_io_handle_t io = NULL;
            esp_lcd_panel_io_spi_config_t ioc = {
                .cs_gpio_num = -1, .dc_gpio_num = PIN_DC, .spi_mode = modes[m],
                .pclk_hz = 10 * 1000 * 1000, .trans_queue_depth = 4,
                .on_color_trans_done = on_done, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
            };
            ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &ioc, &io));
            esp_lcd_panel_handle_t p = NULL;
            esp_lcd_panel_dev_config_t pc = {
                .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
            };
            ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &p));
            esp_lcd_panel_reset(p);
            vTaskDelay(pdMS_TO_TICKS(150));
            esp_lcd_panel_init(p);
            esp_lcd_panel_invert_color(p, true);
            esp_lcd_panel_disp_on_off(p, true);
            ESP_LOGW(TAG, "SPI MODE %d -> screen should be %s, then stripes", modes[m], names[m]);
            fill(p, colors[m]);
            vTaskDelay(pdMS_TO_TICKS(2500));
            stripes(p);
            vTaskDelay(pdMS_TO_TICKS(2500));
            esp_lcd_panel_del(p);
            esp_lcd_panel_io_del(io);
        }
    }
}
