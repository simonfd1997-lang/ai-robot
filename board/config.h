#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ===== Аудио (как на схеме) =====
#define AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Микрофон INMP441
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
// Усилитель MAX98357A
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// ===== Кнопки =====
#define BOOT_BUTTON_GPIO        GPIO_NUM_0    // кнопка BOOT на плате
#define TOUCH_BUTTON_GPIO       GPIO_NUM_17   // сенсор TTP223 (активный HIGH)

// ===== Светодиоды =====
#define BUILTIN_LED_GPIO        GPIO_NUM_48   // RGB на плате (статус)
#define LAMP_GPIO               GPIO_NUM_38   // внешний LED ("den LED")

// ===== OLED SSD1306 128x64 =====
#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// ===== Драйвер моторов MX1508 =====
#define MOTOR_L_IN1 GPIO_NUM_11   // левый мотор
#define MOTOR_L_IN2 GPIO_NUM_12
#define MOTOR_R_IN1 GPIO_NUM_13   // правый мотор
#define MOTOR_R_IN2 GPIO_NUM_14

// Если колесо крутится не в ту сторону — поменяйте false на true
#define MOTOR_L_INVERT false
#define MOTOR_R_INVERT false

#endif // _BOARD_CONFIG_H_
