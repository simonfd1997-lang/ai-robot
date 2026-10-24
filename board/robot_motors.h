#pragma once
// Управление двумя моторами через MX1508 + инструменты MCP,
// чтобы AI мог водить робота голосовыми командами.

#include "mcp_server.h"
#include "config.h"
#include <driver/ledc.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RobotMotors {
private:
    static constexpr const char* TAG = "RobotMotors";
    esp_timer_handle_t stop_timer_ = nullptr;
    int default_speed_ = 70;  // %

    struct Ch { gpio_num_t pin; ledc_channel_t ch; };
    Ch pins_[4] = {
        {MOTOR_L_IN1, LEDC_CHANNEL_2}, {MOTOR_L_IN2, LEDC_CHANNEL_3},
        {MOTOR_R_IN1, LEDC_CHANNEL_4}, {MOTOR_R_IN2, LEDC_CHANNEL_5},
    };

    void SetDuty(int idx, int percent) {
        uint32_t duty = (uint32_t)(percent * 1023 / 100);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, pins_[idx].ch, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, pins_[idx].ch);
    }

    // speed: -100..100 (минус = назад)
    void Drive(int base, bool invert, int speed) {
        if (invert) speed = -speed;
        if (speed > 0)      { SetDuty(base, speed);  SetDuty(base + 1, 0); }
        else if (speed < 0) { SetDuty(base, 0);      SetDuty(base + 1, -speed); }
        else                { SetDuty(base, 0);      SetDuty(base + 1, 0); }
    }

    void Move(int left, int right, int ms) {
        esp_timer_stop(stop_timer_);
        Drive(0, MOTOR_L_INVERT, left);
        Drive(2, MOTOR_R_INVERT, right);
        if (ms > 0) esp_timer_start_once(stop_timer_, (uint64_t)ms * 1000);
    }

public:
    void Stop() {
        esp_timer_stop(stop_timer_);
        for (int i = 0; i < 4; i++) SetDuty(i, 0);
    }

    RobotMotors() {
        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.timer_num = LEDC_TIMER_1;
        t.freq_hz = 20000;
        t.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&t));
        for (auto& p : pins_) {
            ledc_channel_config_t c = {};
            c.gpio_num = p.pin;
            c.speed_mode = LEDC_LOW_SPEED_MODE;
            c.channel = p.ch;
            c.timer_sel = LEDC_TIMER_1;
            c.duty = 0;
            ESP_ERROR_CHECK(ledc_channel_config(&c));
        }
        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) { static_cast<RobotMotors*>(arg)->Stop(); };
        args.arg = this;
        args.name = "motor_stop";
        ESP_ERROR_CHECK(esp_timer_create(&args, &stop_timer_));

        RegisterTools();
        ESP_LOGI(TAG, "Motors ready");
    }

private:
    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();
        auto props = []() {
            return PropertyList({
                Property("duration_ms", kPropertyTypeInteger, 1000, 100, 10000),
            });
        };

        mcp.AddTool("self.robot.move_forward",
            "Робот едет вперёд. Move the robot forward for duration_ms milliseconds.",
            props(), [this](const PropertyList& p) -> ReturnValue {
                Move(default_speed_, default_speed_, p["duration_ms"].value<int>());
                return true;
            });
        mcp.AddTool("self.robot.move_backward",
            "Робот едет назад. Move the robot backward for duration_ms milliseconds.",
            props(), [this](const PropertyList& p) -> ReturnValue {
                Move(-default_speed_, -default_speed_, p["duration_ms"].value<int>());
                return true;
            });
        mcp.AddTool("self.robot.turn_left",
            "Поворот налево на месте. Spin left; ~500 ms is about a quarter turn.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, 500, 100, 5000)}),
            [this](const PropertyList& p) -> ReturnValue {
                Move(-default_speed_, default_speed_, p["duration_ms"].value<int>());
                return true;
            });
        mcp.AddTool("self.robot.turn_right",
            "Поворот направо на месте. Spin right; ~500 ms is about a quarter turn.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, 500, 100, 5000)}),
            [this](const PropertyList& p) -> ReturnValue {
                Move(default_speed_, -default_speed_, p["duration_ms"].value<int>());
                return true;
            });
        mcp.AddTool("self.robot.stop", "Остановить робота. Stop all motors immediately.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                Stop();
                return true;
            });
        mcp.AddTool("self.robot.set_speed",
            "Установить скорость моторов в процентах (30-100). Set motor speed percent.",
            PropertyList({Property("speed", kPropertyTypeInteger, 30, 100)}),
            [this](const PropertyList& p) -> ReturnValue {
                default_speed_ = p["speed"].value<int>();
                return true;
            });
        mcp.AddTool("self.robot.dance",
            "Робот танцует: крутится влево-вправо. Make the robot do a short dance.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                xTaskCreate([](void* arg) {
                    auto* self = static_cast<RobotMotors*>(arg);
                    for (int i = 0; i < 4; i++) {
                        self->Move(80, -80, 0);  vTaskDelay(pdMS_TO_TICKS(300));
                        self->Move(-80, 80, 0);  vTaskDelay(pdMS_TO_TICKS(300));
                    }
                    self->Move(60, 60, 0);   vTaskDelay(pdMS_TO_TICKS(250));
                    self->Move(-60, -60, 0); vTaskDelay(pdMS_TO_TICKS(250));
                    self->Stop();
                    vTaskDelete(nullptr);
                }, "dance", 3072, this, 3, nullptr);
                return true;
            });
    }
};
