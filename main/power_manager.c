#include "power_manager.h"
#include "app_config.h"
#include "mqtt_client_app.h"
#include "ui_oled.h"
#include "keypad.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_wifi.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER";

static int64_t s_last_activity_ms = 0;


static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


/*
 *  刷新系统活动时间
 */

void power_manager_feed_activity(void)
{
    s_last_activity_ms = now_ms();
}


/*
 *  配置 PCF8574T INT 唤醒
 *
 * 接线：
 *      PCF8574T INT -> ESP32-S3 GPIO4
 *
 * 特性：
 *      PCF8574T INT 通常是开漏输出，低电平有效。
 *
 * 睡眠前：
 *      keypad_prepare_sleep_wakeup() 将 P0~P3 拉低；
 *      P4~P7 释放为输入。
 *
 * 用户按键后：
 *      某个列线被拉低；
 *      PCF8574T INT 触发低电平；
 *      ESP32-S3 被 ext0 唤醒。
 */

static esp_err_t power_config_keypad_int_wakeup(void)
{
    esp_err_t ret = keypad_prepare_sleep_wakeup();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "keypad_prepare_sleep_wakeup failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PCF8574_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "wake gpio config failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    /*
     * ext0 唤醒要求 RTC GPIO。
     * 你的板子图里 GPIO4 标注了 RTC，可以用于唤醒。
     */
    rtc_gpio_pullup_en(PCF8574_INT_GPIO);
    rtc_gpio_pulldown_dis(PCF8574_INT_GPIO);

    /*
     * ext0：单个 RTC GPIO 电平唤醒。
     * 第二个参数 0 表示低电平唤醒。
     */
    ret = esp_sleep_enable_ext0_wakeup(PCF8574_INT_GPIO, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "ext0 wakeup config failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG,
             "PCF8574T INT wakeup configured, gpio=%d, active=LOW",
             PCF8574_INT_GPIO);

    return ESP_OK;
}


/*
 *  进入 Deep Sleep
 */

void power_manager_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Prepare to enter deep sleep");

    ui_show("POWER SAVE", "DEEP SLEEP");

    mqtt_client_app_publish_status("sleep");
    vTaskDelay(pdMS_TO_TICKS(300));

    mqtt_client_app_stop();

    esp_wifi_stop();

    esp_err_t wake_ret = power_config_keypad_int_wakeup();
    if (wake_ret != ESP_OK) {
        ESP_LOGW(TAG, "Keypad INT wakeup unavailable, timer wakeup only");
    }

    /*
     * 保留定时器唤醒，避免设备睡死。
     */
    esp_sleep_enable_timer_wakeup(
        (uint64_t)POWER_TIMER_WAKEUP_SEC * 1000000ULL
    );

    ESP_LOGI(TAG,
             "Deep sleep start, keypad_int_gpio=%d, timer=%d sec",
             PCF8574_INT_GPIO,
             POWER_TIMER_WAKEUP_SEC);

    vTaskDelay(pdMS_TO_TICKS(200));

    esp_deep_sleep_start();
}


/*
 *  空闲检测任务
 */

static void power_monitor_task(void *arg)
{
    while (1) {
        int64_t idle_ms = now_ms() - s_last_activity_ms;

        if (idle_ms >= POWER_IDLE_SLEEP_MS) {
            ESP_LOGI(TAG,
                     "System idle %lld ms, enter deep sleep",
                     (long long)idle_ms);

            power_manager_enter_deep_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


/*
 
 *  初始化功耗管理模块
 
 */

esp_err_t power_manager_init(void)
{
    s_last_activity_ms = now_ms();

    xTaskCreate(power_monitor_task,
                "power_monitor_task",
                4096,
                NULL,
                2,
                NULL);

    ESP_LOGI(TAG, "Power manager initialized");

    return ESP_OK;
}