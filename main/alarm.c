#include "alarm.h"
#include "app_config.h"
#include "ui_oled.h"
#include "event_log.h"
#include "power_manager.h"

#include "esp_log.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG0 = "ALARM";
static const char *TAG1= "BUZZER";
// static QueueHandle_t s_tamper_queue = NULL;


/*
 *  蜂鸣器
 */
esp_err_t buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) return ret;
    gpio_set_level(BUZZER_GPIO, 0);
    ESP_LOGI(TAG0, "Buzzer initialized, gpio=%d", BUZZER_GPIO);
    return ESP_OK;
}

//发声
void buzzer_beep(int times,int duration_ms)
{
    for (int i = 0; i < times ; i++){
        gpio_set_level(BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));

        gpio_set_level(BUZZER_GPIO, 0);
    }
}

//被拆长鸣
void buzzer_beep_temper(void)
{
    gpio_set_level(BUZZER_GPIO, 1);
}

/*
 *  防拆 GPIO 中断
 */
// static void IRAM_ATTR tamper_isr_handler(void *arg)
// {
//     uint32_t gpio_num = (uint32_t)arg;

//     if (s_tamper_queue != NULL) {
//         xQueueSendFromISR(s_tamper_queue, &gpio_num, NULL);
//     }
// }


/*
 *  防拆报警任务
 */
// static void tamper_task(void *arg)
// {
//     uint32_t gpio_num;

//     while (1) {
//         if (xQueueReceive(s_tamper_queue, &gpio_num, portMAX_DELAY) == pdTRUE) {
//             /*
//              * 简单消抖。
//              */
//             vTaskDelay(pdMS_TO_TICKS(50));

//             power_manager_feed_activity();

//             ESP_LOGW(TAG1, "Tamper detected, gpio=%lu", (unsigned long)gpio_num);

//             ui_show("WARNING", "TAMPER ALARM");

//             buzzer_beep_temper();

//             /*
//              * 防拆报警作为报警事件上传。
//              */
//             record_event("tamper", "unknown", "sensor", true);
//         }
//     }
// }


/*
 *  初始化报警模块
 */
esp_err_t alarm_init(void)
{
    gpio_config_t buzzer_conf = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&buzzer_conf));
    gpio_set_level(BUZZER_GPIO, 0);

    //暂时搁置
    // gpio_config_t tamper_conf = {
    //     .pin_bit_mask = 1ULL << TAMPER_GPIO,
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_ANYEDGE,
    // };

    // ESP_ERROR_CHECK(gpio_config(&tamper_conf));

    // s_tamper_queue = xQueueCreate(4, sizeof(uint32_t));
    // if (s_tamper_queue == NULL) {
    //     return ESP_ERR_NO_MEM;
    // }

    // /*
    //  * 如果其他模块已经安装过 ISR service，
    //  * gpio_install_isr_service() 可能返回 ESP_ERR_INVALID_STATE。
    //  */
    // esp_err_t ret = gpio_install_isr_service(0);
    // if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    //     return ret;
    // }

    // ESP_ERROR_CHECK(gpio_isr_handler_add(TAMPER_GPIO,
    //                                      tamper_isr_handler,
    //                                      (void *)TAMPER_GPIO));

    // xTaskCreate(tamper_task,
    //             "tamper_task",
    //             4096,
    //             NULL,
    //             5,
    //             NULL);

    ESP_LOGI(TAG1, "Alarm module initialized");

    return ESP_OK;
}