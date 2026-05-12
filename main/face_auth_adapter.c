#include "face_auth_adapter.h"
#include "auth.h"
#include "ui_oled.h"
#include "event_log.h"
#include "power_manager.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "FACE_ADAPTER";

static int64_t s_last_success_ms = 0;
static int64_t s_last_unknown_ms = 0;


static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


esp_err_t face_auth_adapter_init(void)
{
    ESP_LOGI(TAG, "Face auth adapter initialized");
    return ESP_OK;
}


void face_auth_on_recognized(uint16_t face_id, int similarity)
{
    int64_t now = now_ms();

    /*
     * 冷却时间，避免一个人站在摄像头前连续触发开锁。
     */
    if (now - s_last_success_ms < FACE_SUCCESS_COOLDOWN_MS) {
        return;
    }

    s_last_success_ms = now;

    power_manager_feed_activity();

    ESP_LOGI(TAG,
             "Face recognized, face_id=%u, similarity=%d",
             face_id,
             similarity);

    auth_submit_face_id(face_id, similarity);
}


void face_auth_on_unknown(void)
{
    int64_t now = now_ms();

    /*
     * 未知人脸也加冷却时间，避免后台日志被刷爆。
     */
    if (now - s_last_unknown_ms < FACE_UNKNOWN_COOLDOWN_MS) {
        return;
    }

    s_last_unknown_ms = now;

    power_manager_feed_activity();

    ESP_LOGW(TAG, "Unknown face");

    ui_show("FACE UNKNOWN", "TRY AGAIN");

    record_event("face_unknown", "unknown", "face", false);
}