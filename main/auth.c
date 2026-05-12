#include "auth.h"
#include "app_config.h"
#include "ui_oled.h"
#include "lock_ctrl.h"
#include "alarm.h"
#include "event_log.h"
#include "user_db.h"
#include "power_manager.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AUTH";

static int s_fail_count = 0;
static int64_t s_lockout_until_ms = 0;


static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}


/*
 *  认证成功统一处理
 */
static void auth_success(const char *user_id,
                         const char *method,
                         bool alarm_event)
{
    s_fail_count = 0;

    power_manager_feed_activity();

    ui_show("AUTH OK", "OPENING DOOR");

    if (alarm_event) {
        record_event("duress_alarm", user_id, method, true);
    } else {
        record_event("unlock", user_id, method, false);
    }

    /*
     * 胁迫密码要求静默报警，因此本地提示不要太异常。
     */
    buzzer_beep(1, 80);

    lock_ctrl_open();
}


/*
 *  认证失败统一处理
 */
static void auth_fail(const char *method)
{
    s_fail_count++;

    power_manager_feed_activity();

    ESP_LOGW(TAG,
             "Auth failed, method=%s, fail_count=%d",
             method ? method : "unknown",
             s_fail_count);

    ui_show("AUTH FAILED", "TRY AGAIN");

    record_event("auth_fail", "unknown", method ? method : "unknown", false);

    buzzer_beep(2, 80);

    if (s_fail_count >= MAX_FAIL_COUNT) {
        s_lockout_until_ms = now_ms() + LOCKOUT_TIME_MS;
        s_fail_count = 0;

        ui_show("SYSTEM LOCKED", "60 SECONDS");

        record_event("lockout", "unknown", method ? method : "unknown", true);

        buzzer_beep(4, 150);
    }
}


esp_err_t auth_init(void)
{
    s_fail_count = 0;
    s_lockout_until_ms = 0;

    ESP_ERROR_CHECK(user_db_init());

    ESP_LOGI(TAG, "Auth module initialized");

    return ESP_OK;
}


bool auth_system_is_locked(void)
{
    return now_ms() < s_lockout_until_ms;
}


/*
 *  密码认证
 */
void auth_submit_password(const char *password)
{
    if (password == NULL) {
        return;
    }

    if (auth_system_is_locked()) {
        ui_show("SYSTEM LOCKED", "PLEASE WAIT");
        return;
    }

    if (user_db_is_duress_password(password)) {
        /*
         * 胁迫密码：开锁 + 后台报警。
         */
        auth_success("local_user", "password", true);
        return;
    }

    const user_record_t *user = user_db_find_by_password(password);
    if (user != NULL) {
        auth_success(user->user_id, "password", false);
        return;
    }

    auth_fail("password");
}


/*
 *  刷卡认证
 */
void auth_submit_card(const uint8_t *uid, uint8_t uid_len)
{
    if (uid == NULL || uid_len == 0) {
        return;
    }

    char uid_str[40] = {0};
    user_db_uid_to_string(uid, uid_len, uid_str, sizeof(uid_str));

    ESP_LOGI(TAG, "Card UID: %s", uid_str);

    if (auth_system_is_locked()) {
        ui_show("SYSTEM LOCKED", "PLEASE WAIT");
        return;
    }

    const user_record_t *user = user_db_find_by_card_uid(uid, uid_len);
    if (user != NULL) {
        auth_success(user->user_id, "card", false);
        return;
    }

    ui_show("CARD DENIED", uid_str);
    record_event("card_denied", "unknown", "card", false);

    auth_fail("card");
}


/*
 *  指纹认证
 */
void auth_submit_fingerprint(uint16_t finger_id, uint16_t confidence)
{
    ESP_LOGI(TAG,
             "Fingerprint detected, id=%u, confidence=%u",
             finger_id,
             confidence);

    if (auth_system_is_locked()) {
        ui_show("SYSTEM LOCKED", "PLEASE WAIT");
        return;
    }

    const user_record_t *user = user_db_find_by_finger_id(finger_id);
    if (user != NULL) {
        auth_success(user->user_id, "finger", false);
        return;
    }

    ui_show("FINGER DENIED", "UNKNOWN ID");
    record_event("finger_denied", "unknown", "finger", false);

    auth_fail("finger");
}


/*
 *  人脸认证
 */
void auth_submit_face_id(uint16_t face_id, int similarity)
{
    ESP_LOGI(TAG,
             "Face recognized, face_id=%u, similarity=%d",
             face_id,
             similarity);

    if (auth_system_is_locked()) {
        ui_show("SYSTEM LOCKED", "PLEASE WAIT");
        return;
    }

    const user_record_t *user = user_db_find_by_face_id(face_id);
    if (user != NULL) {
        auth_success(user->user_id, "face", false);
        return;
    }

    ui_show("FACE DENIED", "UNKNOWN ID");
    record_event("face_denied", "unknown", "face", false);

    auth_fail("face");
}