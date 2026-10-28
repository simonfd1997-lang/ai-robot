// Karen: sassy English-speaking desk robot on ESP32-S3.
// Every module is optional: missing I2C devices are skipped, unconnected
// sensor pins are pulled down, the camera is used only if detected.

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include "karen_face.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TAG "Karen"

// ---------------- small I2C helpers (never abort if a device is missing) ----------------
class I2cDev {
public:
    i2c_master_dev_handle_t h = nullptr;
    bool Attach(i2c_master_bus_handle_t bus, uint8_t addr) {
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) return false;
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = addr;
        cfg.scl_speed_hz = 100000;
        return i2c_master_bus_add_device(bus, &cfg, &h) == ESP_OK;
    }
    bool ok() const { return h != nullptr; }
    bool Write(const uint8_t* d, size_t n) { return h && i2c_master_transmit(h, d, n, 50) == ESP_OK; }
    bool WriteReg(uint8_t reg, uint8_t v) { uint8_t b[2] = {reg, v}; return Write(b, 2); }
    bool Read(uint8_t* d, size_t n) { return h && i2c_master_receive(h, d, n, 50) == ESP_OK; }
    bool ReadRegs(uint8_t reg, uint8_t* d, size_t n) {
        return h && i2c_master_transmit_receive(h, &reg, 1, d, n, 50) == ESP_OK;
    }
};

class Pca9685 {
public:
    I2cDev dev;
    bool Init(i2c_master_bus_handle_t bus) {
        if (!dev.Attach(bus, 0x40)) return false;
        dev.WriteReg(0x00, 0x10);                 // MODE1: sleep
        dev.WriteReg(0xFE, 5);                    // prescale -> ~1 kHz PWM
        dev.WriteReg(0x00, 0x20);                 // wake, auto-increment
        vTaskDelay(pdMS_TO_TICKS(2));
        dev.WriteReg(0x00, 0xA0);                 // restart
        dev.WriteReg(0x01, 0x04);                 // MODE2: totem pole outputs
        for (int ch = 0; ch < 16; ch++) Set(ch, 0);
        return true;
    }
    // duty 0..4095
    void Set(int ch, int duty) {
        if (!dev.ok()) return;
        uint8_t b[5] = {(uint8_t)(0x06 + 4 * ch), 0, 0, 0, 0};
        if (duty >= 4095) b[2] = 0x10;            // full on
        else if (duty <= 0) b[4] = 0x10;          // full off
        else { b[3] = duty & 0xFF; b[4] = (duty >> 8) & 0x0F; }
        dev.Write(b, 5);
    }
};

// ---------------- board ----------------
class KarenBoard : public WifiBoard {
private:
    Button boot_button_;
    Button touch_button_;
    KarenDisplay* display_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;

    Pca9685 pca_;
    I2cDev mpu_, aht_, bh_;
    adc_oneshot_unit_handle_t adc_ = nullptr;

    esp_timer_handle_t stop_timer_ = nullptr;
    int speed_ = 70;
    volatile bool moving_forward_ = false;
    bool led_on_[2] = {false, false};

    float battery_v_ = 0;
    bool battery_seen_ = false;   // a real battery was measured (>3.7 V for 10 s)
    float temp_c_ = NAN, humidity_ = NAN, lux_ = NAN;
    int64_t last_pir_wake_ = -1, last_dizzy_ = 0, last_lowbat_ = 0;

    // ---------- hardware init ----------
    void InitializeI2c() {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = I2C_NUM_0;
        cfg.sda_io_num = I2C_SDA_PIN;
        cfg.scl_io_num = I2C_SCL_PIN;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = 1;
        if (i2c_new_master_bus(&cfg, &i2c_bus_) != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus init failed");
            i2c_bus_ = nullptr;
        }
    }

    void InitializeDisplay() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new KarenDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeCamera() {
        if (i2c_bus_ == nullptr || i2c_master_probe(i2c_bus_, 0x30, 50) != ESP_OK) {
            ESP_LOGI(TAG, "No camera detected");
            return;
        }
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;          // use the already initialized I2C bus
        config.pin_sccb_scl = -1;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.ledc_timer = LEDC_TIMER_0;
        config.ledc_channel = LEDC_CHANNEL_0;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
        ESP_LOGI(TAG, "Camera initialized");
    }

    void InitializeI2cDevices() {
        if (i2c_bus_ == nullptr) return;
        if (pca_.Init(i2c_bus_)) {
            ESP_LOGI(TAG, "PCA9685 found");
            pca_.Set(PCA_CH_BACKLIGHT, 4095);
        }
        if (mpu_.Attach(i2c_bus_, 0x68)) { mpu_.WriteReg(0x6B, 0x00); ESP_LOGI(TAG, "MPU6050 found"); }
        if (aht_.Attach(i2c_bus_, 0x38)) {
            uint8_t init[3] = {0xBE, 0x08, 0x00}; aht_.Write(init, 3); ESP_LOGI(TAG, "AHT20 found");
        }
        if (bh_.Attach(i2c_bus_, 0x23)) {
            uint8_t on = 0x01, cont = 0x10; bh_.Write(&on, 1); bh_.Write(&cont, 1); ESP_LOGI(TAG, "BH1750 found");
        }
    }

    void InitializeInputs() {
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << PIR_GPIO) | (1ULL << CLIFF_LEFT_GPIO) | (1ULL << CLIFF_RIGHT_GPIO) | (1ULL << TOUCH_GPIO);
        io.mode = GPIO_MODE_INPUT;
        io.pull_down_en = GPIO_PULLDOWN_ENABLE;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);

        adc_oneshot_unit_init_cfg_t unit = {};
        unit.unit_id = ADC_UNIT_1;
        unit.ulp_mode = ADC_ULP_MODE_DISABLE;
        if (adc_oneshot_new_unit(&unit, &adc_) == ESP_OK) {
            adc_oneshot_chan_cfg_t ch = {};
            ch.atten = ADC_ATTEN_DB_12;
            ch.bitwidth = ADC_BITWIDTH_12;
            adc_oneshot_config_channel(adc_, ADC_CHANNEL_2, &ch);   // GPIO3
        } else {
            adc_ = nullptr;
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnClick([this]() {
            if (display_) display_->Trigger(FACE_PETTED, 3500);
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.WakeWordInvoke("(the user just petted the top of your head)");
            }
        });
    }

    // ---------- motors ----------
    void Drive(int in1, int in2, bool invert, int speed) {
        if (invert) speed = -speed;
        int duty = std::min(100, std::abs(speed)) * 4095 / 100;
        if (speed > 0) { pca_.Set(in1, duty); pca_.Set(in2, 0); }
        else if (speed < 0) { pca_.Set(in1, 0); pca_.Set(in2, duty); }
        else { pca_.Set(in1, 0); pca_.Set(in2, 0); }
    }
    void Move(int left, int right, int ms) {
        esp_timer_stop(stop_timer_);
        moving_forward_ = left > 0 && right > 0;
        Drive(PCA_CH_MOTOR_L_IN1, PCA_CH_MOTOR_L_IN2, MOTOR_L_INVERT, left);
        Drive(PCA_CH_MOTOR_R_IN1, PCA_CH_MOTOR_R_IN2, MOTOR_R_INVERT, right);
        if (ms > 0) esp_timer_start_once(stop_timer_, (uint64_t)ms * 1000);
    }
public:
    void Stop() {
        esp_timer_stop(stop_timer_);
        moving_forward_ = false;
        for (int ch = PCA_CH_MOTOR_L_IN1; ch <= PCA_CH_MOTOR_R_IN2; ch++) pca_.Set(ch, 0);
    }
private:

    // ---------- sensors ----------
    static void SensorTask(void* arg) { static_cast<KarenBoard*>(arg)->SensorLoop(); }

    void SensorLoop() {
        int tick = 0, dark_ticks = 0, shake_hits = 0;
        bool pir_prev = false;
        float bat_avg = 0;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(100));
            tick++;
            auto& app = Application::GetInstance();
            bool idle = app.GetDeviceState() == kDeviceStateIdle;
            int64_t now = esp_timer_get_time() / 1000000;

            // Table edge: stop and back off
            if (moving_forward_ && (gpio_get_level(CLIFF_LEFT_GPIO) || gpio_get_level(CLIFF_RIGHT_GPIO))) {
                Move(-speed_, -speed_, 400);
                if (display_) display_->Trigger(FACE_SCARED, 2000);
            }

            // Someone walked in
            bool pir = gpio_get_level(PIR_GPIO);
            if (pir && !pir_prev) {
                if (display_) display_->Trigger(FACE_HELLO, 3000);
                if (idle && (last_pir_wake_ < 0 || now - last_pir_wake_ > 600)) {
                    last_pir_wake_ = now;
                    app.WakeWordInvoke("(someone just walked into the room, greet them)");
                }
            }
            pir_prev = pir;

            // Shaken / picked up
            if (mpu_.ok()) {
                uint8_t d[6];
                if (mpu_.ReadRegs(0x3B, d, 6)) {
                    float ax = (int16_t)(d[0] << 8 | d[1]) / 16384.0f;
                    float ay = (int16_t)(d[2] << 8 | d[3]) / 16384.0f;
                    float az = (int16_t)(d[4] << 8 | d[5]) / 16384.0f;
                    float g = sqrtf(ax * ax + ay * ay + az * az);
                    shake_hits = fabsf(g - 1.0f) > 0.7f ? shake_hits + 1 : std::max(0, shake_hits - 1);
                    if (shake_hits >= 4) {
                        shake_hits = 0;
                        if (display_) display_->Trigger(FACE_DIZZY, 3000);
                        if (idle && now - last_dizzy_ > 60) {
                            last_dizzy_ = now;
                            app.WakeWordInvoke("(someone is shaking you and you feel dizzy)");
                        }
                    }
                }
            }

            if (tick % 10 == 0) {   // once per second
                if (bh_.ok()) {
                    uint8_t d[2];
                    if (bh_.Read(d, 2)) lux_ = ((d[0] << 8) | d[1]) / 1.2f;
                    dark_ticks = (!std::isnan(lux_) && lux_ < 3.0f) ? dark_ticks + 1 : 0;
                    if (dark_ticks >= 5 && idle && display_) display_->Trigger(FACE_SLEEPY, 1500);
                }
                if (adc_) {
                    int raw = 0;
                    if (adc_oneshot_read(adc_, ADC_CHANNEL_2, &raw) == ESP_OK) {
                        float v = raw / 4095.0f * 3.1f * 2.0f;
                        bat_avg = bat_avg == 0 ? v : bat_avg * 0.9f + v * 0.1f;
                        battery_v_ = bat_avg;
                    }
                    static int healthy_s = 0;
                    healthy_s = battery_v_ > 3.7f && battery_v_ < 4.4f ? healthy_s + 1 : 0;
                    if (healthy_s >= 10) battery_seen_ = true;
                    if (battery_seen_ && battery_v_ < 3.45f && idle && now - last_lowbat_ > 1800) {
                        last_lowbat_ = now;
                        if (display_) display_->Trigger(FACE_LOWBAT, 6000);
                        app.WakeWordInvoke("(your battery is almost empty, complain that you are hungry and ask to be charged)");
                    }
                }
            }
            if (tick % 50 == 0 && aht_.ok()) {   // every 5 s
                uint8_t cmd[3] = {0xAC, 0x33, 0x00};
                if (aht_.Write(cmd, 3)) {
                    vTaskDelay(pdMS_TO_TICKS(90));
                    uint8_t d[6];
                    if (aht_.Read(d, 6) && !(d[0] & 0x80)) {
                        uint32_t hr = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
                        uint32_t tr = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
                        humidity_ = hr * 100.0f / 1048576.0f;
                        temp_c_ = tr * 200.0f / 1048576.0f - 50.0f;
                    }
                }
            }
        }
    }

    // ---------- MCP tools (what the AI can do) ----------
    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();
        auto dur = [](int def, int max) {
            return PropertyList({Property("duration_ms", kPropertyTypeInteger, def, 100, max)});
        };
        mcp.AddTool("self.robot.move_forward", "Drive forward for duration_ms milliseconds.", dur(1000, 10000),
            [this](const PropertyList& p) -> ReturnValue { Move(speed_, speed_, p["duration_ms"].value<int>()); return true; });
        mcp.AddTool("self.robot.move_backward", "Drive backward for duration_ms milliseconds.", dur(1000, 10000),
            [this](const PropertyList& p) -> ReturnValue { Move(-speed_, -speed_, p["duration_ms"].value<int>()); return true; });
        mcp.AddTool("self.robot.turn_left", "Spin left in place; ~500 ms is a quarter turn.", dur(500, 5000),
            [this](const PropertyList& p) -> ReturnValue { Move(-speed_, speed_, p["duration_ms"].value<int>()); return true; });
        mcp.AddTool("self.robot.turn_right", "Spin right in place; ~500 ms is a quarter turn.", dur(500, 5000),
            [this](const PropertyList& p) -> ReturnValue { Move(speed_, -speed_, p["duration_ms"].value<int>()); return true; });
        mcp.AddTool("self.robot.stop", "Stop the wheels immediately.", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { Stop(); return true; });
        mcp.AddTool("self.robot.set_speed", "Set wheel speed in percent (30-100).",
            PropertyList({Property("speed", kPropertyTypeInteger, 30, 100)}),
            [this](const PropertyList& p) -> ReturnValue { speed_ = p["speed"].value<int>(); return true; });
        mcp.AddTool("self.robot.dance", "Do a short sassy dance.", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (display_) display_->Trigger(FACE_LAUGHING, 3500);
                xTaskCreate([](void* arg) {
                    auto* b = static_cast<KarenBoard*>(arg);
                    for (int i = 0; i < 4; i++) {
                        b->Move(80, -80, 0); vTaskDelay(pdMS_TO_TICKS(300));
                        b->Move(-80, 80, 0); vTaskDelay(pdMS_TO_TICKS(300));
                    }
                    b->Stop();
                    vTaskDelete(nullptr);
                }, "dance", 3072, this, 3, nullptr);
                return true;
            });
        mcp.AddTool("self.light.set",
            "Turn Karen's lights on or off. light: 1 or 2 (0 = both). on: true/false.",
            PropertyList({Property("light", kPropertyTypeInteger, 0, 0, 2), Property("on", kPropertyTypeBoolean)}),
            [this](const PropertyList& p) -> ReturnValue {
                int l = p["light"].value<int>(); bool on = p["on"].value<bool>();
                for (int i = 0; i < 2; i++) if (l == 0 || l == i + 1) {
                    led_on_[i] = on; pca_.Set(i == 0 ? PCA_CH_LED1 : PCA_CH_LED2, on ? 4095 : 0);
                }
                return true;
            });
        mcp.AddTool("self.sensors.get_status",
            "Read Karen's sensors: room temperature (C), humidity (%), light level (lux), battery voltage. "
            "Missing sensors return null.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                char buf[200];
                auto fmt = [](float v, char* out) { if (std::isnan(v)) strcpy(out, "null"); else sprintf(out, "%.1f", v); };
                char t[16], h[16], l[16], b[16];
                fmt(temp_c_, t); fmt(humidity_, h); fmt(lux_, l);
                fmt(battery_seen_ ? battery_v_ : NAN, b);
                snprintf(buf, sizeof(buf), "{\"temperature_c\":%s,\"humidity_pct\":%s,\"light_lux\":%s,\"battery_v\":%s}", t, h, l, b);
                return std::string(buf);
            });
    }

public:
    KarenBoard() : boot_button_(BOOT_BUTTON_GPIO), touch_button_(TOUCH_GPIO, true) {
        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) { static_cast<KarenBoard*>(arg)->Stop(); };
        args.arg = this;
        args.name = "karen_stop";
        esp_timer_create(&args, &stop_timer_);

        InitializeI2c();
        InitializeDisplay();
        InitializeI2cDevices();
        InitializeCamera();
        InitializeInputs();
        InitializeButtons();
        InitializeTools();
        xTaskCreate(SensorTask, "karen_sensors", 4096, this, 2, nullptr);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
    virtual Camera* GetCamera() override { return camera_; }
};

DECLARE_BOARD(KarenBoard);
