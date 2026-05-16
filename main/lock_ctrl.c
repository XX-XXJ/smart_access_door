#include "lock_ctrl.h"
#include "app_config.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LOCK_CTRL";


/*
 *  舵机角度转换为 PWM 占空比
 */
static uint32_t servo_angle_to_duty(int angle)
{
    if (angle < 0) {
        angle = 0;
    }

    if (angle > 180) {
        angle = 180;
    }

    int pulse_us = SERVO_MIN_US +(SERVO_MAX_US - SERVO_MIN_US) * angle / 180;

    /*
     * 50Hz PWM 周期为 20ms。
     * LEDC 分辨率使用 13 bit，即最大 duty 为 8191。
     */
    uint32_t duty = (uint32_t)((pulse_us * 8191) / 20000);

    return duty;
}


static void servo_set_angle(int angle)
{
    uint32_t duty = servo_angle_to_duty(angle);

    ledc_set_duty(LEDC_LOW_SPEED_MODE,
                  LEDC_CHANNEL_0,
                  duty);

    ledc_update_duty(LEDC_LOW_SPEED_MODE,
                     LEDC_CHANNEL_0);
}


esp_err_t lock_ctrl_init(void)
{
#if USE_SERVO_LOCK
    /*
     * 舵机 PWM 初始化。
     */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    servo_set_angle(SERVO_LOCK_ANGLE);

    ESP_LOGI(TAG, "Servo lock initialized");
#else
    gpio_config_t relay_conf = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&relay_conf));
    gpio_set_level(RELAY_GPIO, 0);

    ESP_LOGI(TAG, "Relay lock initialized");
#endif

    return ESP_OK;
}


/*
 *  执行开锁动作
 */
void lock_ctrl_open(void)
{
    ESP_LOGI(TAG, "Door unlock");

#if USE_SERVO_LOCK
    servo_set_angle(SERVO_UNLOCK_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(UNLOCK_HOLD_MS));
    servo_set_angle(SERVO_LOCK_ANGLE);
#else
    gpio_set_level(RELAY_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(UNLOCK_HOLD_MS));
    gpio_set_level(RELAY_GPIO, 0);
#endif

    ESP_LOGI(TAG, "Door locked again");
}