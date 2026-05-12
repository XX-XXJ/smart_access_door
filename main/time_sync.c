#include "time_sync.h"
#include "app_config.h"

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TIME_SYNC";


/*
 *  判断系统时间是否有效
 *
 * 这里用年份 >= 2024 作为判断条件。
 * 因为 ESP32 上电后若没有校时，默认时间通常不是真实日期。
 */
bool time_sync_is_valid(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);
    localtime_r(&now, &timeinfo);

    return timeinfo.tm_year >= (2024 - 1900);
}


/*
 *  初始化 SNTP 时间同步
 */
esp_err_t time_sync_init(void)
{
    /*
     * 设置时区。
     * 中国大陆使用 CST-8。
     */
    setenv("TZ", TIMEZONE_STRING, 1);
    tzset();

    ESP_LOGI(TAG, "Initializing SNTP, server=%s", NTP_SERVER_NAME);

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NTP_SERVER_NAME);
        esp_sntp_init();
    }

    int retry = 0;
    const int retry_count = 15;

    while (!time_sync_is_valid() && retry < retry_count) {
        ESP_LOGI(TAG,
                 "Waiting for time sync... %d/%d",
                 retry + 1,
                 retry_count);

        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (time_sync_is_valid()) {
        char time_str[32] = {0};
        time_sync_get_datetime(time_str, sizeof(time_str));

        ESP_LOGI(TAG, "Time synchronized: %s", time_str);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Time sync timeout");
    return ESP_ERR_TIMEOUT;
}


/*
 *  获取当前时间字符串
 */
void time_sync_get_datetime(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if (time_sync_is_valid()) {
        time_t now = 0;
        struct tm timeinfo = {0};

        time(&now);
        localtime_r(&now, &timeinfo);

        strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
        return;
    }

    /*
     * 如果没有同步到网络时间，用系统运行时间兜底。
     */
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    snprintf(out, out_size, "UNSYNCED_%lld", (long long)uptime_ms);
}