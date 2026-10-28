#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ===== Karen: ESP32-S3 N16R8 (+ optional OV2640 camera) =====

// Audio (INMP441 mic + MAX98357A amp)
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_1
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_2
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_42
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_39
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_40
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_41

// Buttons / LEDs
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define BUILTIN_LED_GPIO        GPIO_NUM_48

// Display ST7789 1.3" 240x240 (no CS pin, RES tied to EN, BLK to 3V3 or PCA9685 CH6)
#define DISPLAY_CLK_PIN         GPIO_NUM_19
#define DISPLAY_MOSI_PIN        GPIO_NUM_20
#define DISPLAY_DC_PIN          GPIO_NUM_47
#define DISPLAY_CS_PIN          GPIO_NUM_NC
#define DISPLAY_RST_PIN         GPIO_NUM_NC
#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          240
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0
#define DISPLAY_SPI_MODE        3
#define DISPLAY_RGB_ORDER       LCD_RGB_ELEMENT_ORDER_RGB

// Shared I2C bus (camera SCCB + PCA9685, MPU6050, AHT20, BH1750, VL53L0X)
#define I2C_SDA_PIN             GPIO_NUM_4
#define I2C_SCL_PIN             GPIO_NUM_5

// Camera OV2640 (only used if detected on the I2C bus)
#define CAMERA_PIN_D0 GPIO_NUM_11
#define CAMERA_PIN_D1 GPIO_NUM_9
#define CAMERA_PIN_D2 GPIO_NUM_8
#define CAMERA_PIN_D3 GPIO_NUM_10
#define CAMERA_PIN_D4 GPIO_NUM_12
#define CAMERA_PIN_D5 GPIO_NUM_18
#define CAMERA_PIN_D6 GPIO_NUM_17
#define CAMERA_PIN_D7 GPIO_NUM_16
#define CAMERA_PIN_XCLK GPIO_NUM_15
#define CAMERA_PIN_PCLK GPIO_NUM_13
#define CAMERA_PIN_VSYNC GPIO_NUM_6
#define CAMERA_PIN_HREF GPIO_NUM_7
#define CAMERA_PIN_PWDN GPIO_NUM_NC
#define CAMERA_PIN_RESET GPIO_NUM_NC
#define XCLK_FREQ_HZ 20000000

// Sensors on GPIO
#define TOUCH_GPIO              GPIO_NUM_14   // TTP223 (active high)
#define PIR_GPIO                GPIO_NUM_46   // PIR (active high)
#define CLIFF_LEFT_GPIO         GPIO_NUM_21   // TCRT5000 DO (HIGH = no table)
#define CLIFF_RIGHT_GPIO        GPIO_NUM_38
#define BATTERY_ADC_GPIO        GPIO_NUM_3    // divider 100k/100k

// PCA9685 channels
#define PCA_CH_MOTOR_L_IN1      0
#define PCA_CH_MOTOR_L_IN2      1
#define PCA_CH_MOTOR_R_IN1      2
#define PCA_CH_MOTOR_R_IN2      3
#define PCA_CH_LED1             4
#define PCA_CH_LED2             5
#define PCA_CH_BACKLIGHT        6

// Flip if a wheel turns the wrong way
#define MOTOR_L_INVERT false
#define MOTOR_R_INVERT false

#endif // _BOARD_CONFIG_H_
