#include "event_log.h"
#include "local_record_db.h"
#include "mqtt_client_app.h"
#include "time_sync.h"
#include "app_config.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EVENT_LOG";


static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


/*
 *  MQTT 补传任务
 *
 * 当前底层本地缓存为 CSV 文件。
 *
 * 任务逻辑：
 * 1. 判断 MQTT 是否连接；
 * 2. 从 CSV 查询一条 uploaded=0 的记录；
 * 3. 把该记录改成 uploaded=2；
 * 4. 调用 MQTT publish；
 * 5. 等 MQTT_EVENT_PUBLISHED 后，由 mqtt_client_app.c 标记 uploaded=1。
 */
static void mqtt_upload_task(void *arg)
{
    while (1) {
        if (mqtt_client_app_is_connected()) {
            pending_record_t record = {0};

            esp_err_t ret = local_record_db_get_pending(&record);

            if (ret == ESP_OK) {
                ESP_LOGI(TAG,
                         "Pending record found, id=%d",
                         record.id);

                ret = local_record_db_mark_publishing(record.id);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG,
                             "Mark publishing failed, id=%d",
                             record.id);

                    vTaskDelay(pdMS_TO_TICKS(MQTT_UPLOAD_INTERVAL_MS));
                    continue;
                }

                ret = mqtt_client_app_publish_event(record.id, record.json);

                if (ret != ESP_OK) {
                    local_record_db_increment_retry(record.id);

                    ESP_LOGW(TAG,
                             "MQTT publish failed, record restored pending, id=%d",
                             record.id);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_UPLOAD_INTERVAL_MS));
    }
}


/*
 *  初始化事件日志模块
 */
esp_err_t event_log_init(void)
{
    /*
     * 初始化本地 CSV 缓存。
     * 内部会自动挂载 SPIFFS 并创建 door_records.csv。
     */
    esp_err_t ret = local_record_db_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Local CSV record init failed: %s",
                 esp_err_to_name(ret));

        return ret;
    }

    /*
     * 创建 MQTT 补传任务。
     */
    xTaskCreate(mqtt_upload_task,
                "mqtt_upload_task",
                6144,
                NULL,
                4,
                NULL);

    ESP_LOGI(TAG, "Event log initialized with CSV + MQTT");

    return ESP_OK;
}


/*
 *  记录一条门禁事件
 */
void record_event(const char *event_type,
                  const char *user_id,
                  const char *method,
                  bool alarm)
{
    char time_str[32] = {0};
    int64_t uptime_ms = now_ms();

    time_sync_get_datetime(time_str, sizeof(time_str));

    ESP_LOGI(TAG,
             "record_event: event=%s, user=%s, method=%s, alarm=%s, time=%s",
             event_type ? event_type : "unknown",
             user_id ? user_id : "unknown",
             method ? method : "unknown",
             alarm ? "true" : "false",
             time_str);

    esp_err_t ret = local_record_db_insert_event(event_type,
                                                 user_id,
                                                 method,
                                                 alarm,
                                                 time_str,
                                                 uptime_ms);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Save event to CSV failed: %s",
                 esp_err_to_name(ret));
    }
}