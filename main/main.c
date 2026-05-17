#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"

#include "app_config.h"

#include "ui_oled.h"
#include "keypad.h"
#include "lock_ctrl.h"
#include "alarm.h"
#include "auth.h"
#include "user_db.h"

#include "rfid_mfrc522.h"
#include "fingerprint_as608.h"

#include "camera_ov2640.h"
#include "camera_http_server.h"
#include "face_auth_adapter.h"
#include "tflm_face_recognition.h"

#include "wifi_net.h"
#include "time_sync.h"

#include "mqtt_client_app.h"
#include "event_log.h"
#include "power_manager.h"

static const char *TAG = "MAIN";


/*
 *  NVS 初始化
 *
 * Wi-Fi 和本地用户数据库 user_db 都会使用 NVS。
 */
static esp_err_t app_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}


/*
 *  键盘任务
 *
 * 普通模式：
 *      输入密码后按 # 认证；
 *      按 * 清除输入；
 *      按 A 进入管理员密码输入。
 *
 * 管理员模式：
 *      A：添加 / 更新 user001；
 *      B：录入 user001 指纹；
 *      C：删除 user001 指纹；
 *      1：删除 user001；
 *      2：恢复默认用户表；
 *      3：模拟人脸识别 face_id=1；
 *      D：退出管理员模式。
 */
static void keypad_task(void *arg)
{
    char input_buf[PASSWORD_MAX_LEN + 1] = {0};
    int input_len = 0;

    bool admin_password_mode = false;
    bool admin_mode = false;

    ui_show("SMART DOOR", "INPUT PASSWORD");

    while (1) {
        char key = keypad_get_key();

        if (key != '\0') {
            /*
             * 有按键说明用户正在操作，刷新空闲计时。
             */
            power_manager_feed_activity();

            ESP_LOGI(TAG, "Key pressed: %c", key);

            /*
             * ====================================================
             * 管理员模式处理
             * ====================================================
             */
            if (admin_mode) {
                if (key == 'A') {
                    /*
                     * 添加 / 更新演示用户 user002。
                     */
                    user_record_t user = {
                        .used = true,
                        .user_id = "user002",
                        .name = "Student",
                        .password = NORMAL_PASSWORD,
                        .card_uid = {0xDE, 0xAD, 0xBE, 0xEF},
                        .card_uid_len = 4,
                        .finger_id = 1,
                        .has_finger = true,
                        .face_id = 1,
                        .has_face = true,
                        .is_admin = false,
                    };

                    esp_err_t ret = user_db_add_or_update(&user);

                    if (ret == ESP_OK) {
                        ui_show("USER SAVED", "USER002");
                        record_event("user_save", "user002", "admin", false);
                    } else {
                        ui_show("USER SAVE FAIL", "TRY AGAIN");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == 'B') {
                    /*
                     * 录入指纹 ID 1，并绑定给 user002。
                     */
                    ui_show("ENROLL FINGER", "SEE SERIAL");

                    esp_err_t ret = fingerprint_as608_enroll(1);

                    if (ret == ESP_OK) {
                        user_db_bind_finger("user002", 1);

                        ui_show("ENROLL OK", "FINGER ID 1");
                        record_event("finger_enroll", "user001", "admin", false);
                    } else {
                        ui_show("ENROLL FAIL", "TRY AGAIN");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == 'C') {
                    /*
                     * 删除指纹 ID 1，并解绑 user002 的指纹。
                     */
                    ui_show("DELETE FINGER", "ID 1");

                    esp_err_t ret = fingerprint_as608_delete(1);

                    if (ret == ESP_OK) {
                        user_db_unbind_finger("user001");

                        ui_show("DELETE OK", "FINGER ID 1");
                        record_event("finger_delete", "user001", "admin", false);
                    } else {
                        ui_show("DELETE FAIL", "TRY AGAIN");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == '1') {
                    /*
                     * 删除普通用户 user002。
                     */
                    esp_err_t ret = user_db_delete_by_user_id("user002");

                    if (ret == ESP_OK) {
                        ui_show("USER DELETED", "USER002");
                        record_event("user_delete", "user002", "admin", false);
                    } else {
                        ui_show("DELETE FAIL", "USER002");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == '2') {
                    /*
                     * 恢复默认用户表。
                     */
                    esp_err_t ret = user_db_reset_default();

                    if (ret == ESP_OK) {
                        ui_show("DB RESET", "DEFAULT OK");
                        record_event("user_db_reset", "admin", "admin", false);
                    } else {
                        ui_show("DB RESET FAIL", "TRY AGAIN");
                    }

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == '3') {
                    /*
                     * 临时测试人脸认证链路。
                     * 真正运行时，face_id 应由 TFLM 模型输出。
                     */
                    face_auth_on_recognized(1, 90);

                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                    continue;
                }

                if (key == 'D') {
                    /*
                     * 退出管理员模式。
                     */
                    admin_mode = false;
                    admin_password_mode = false;
                    memset(input_buf, 0, sizeof(input_buf));
                    input_len = 0;

                    ui_show("EXIT ADMIN", "INPUT PASSWORD");
                    continue;
                }

                ui_show("ADMIN MODE", "A ADD B ENR C DEL");
                continue;
            }

            /*
             * ====================================================
             * 普通模式处理
             * ====================================================
             */
            if (key == 'A') {
                /*
                 * 进入管理员密码输入模式。
                 */
                admin_password_mode = true;
                memset(input_buf, 0, sizeof(input_buf));
                input_len = 0;

                ui_show("ADMIN PASS", "INPUT PASSWORD");
                continue;
            }

            if (key >= '0' && key <= '9') {
                if (input_len < PASSWORD_MAX_LEN) {
                    input_buf[input_len++] = key;
                    input_buf[input_len] = '\0';

                    char mask[PASSWORD_MAX_LEN + 1] = {0};
                    memset(mask, '*', input_len);
                    mask[input_len] = '\0';

                    if (admin_password_mode) {
                        ui_show("ADMIN PASS", mask);
                    } else {
                        ui_show("PASSWORD", mask);
                    }
                } else {
                    ui_show("INPUT TOO LONG", "PRESS CLEAR");
                    buzzer_beep(1, 50);
                }
            } else if (key == '*') {
                memset(input_buf, 0, sizeof(input_buf));
                input_len = 0;

                ui_show("INPUT CLEARED", "INPUT PASSWORD");
            } else if (key == '#') {
                if (input_len == 0) {
                    ui_show("NO INPUT", "INPUT PASSWORD");
                    buzzer_beep(1, 50);
                } else {
                    if (admin_password_mode) {
                        if (user_db_is_admin_password(input_buf)) {
                            admin_mode = true;
                            admin_password_mode = false;

                            ui_show("ADMIN OK", "A ADD B ENR C DEL");
                            record_event("admin_login", "admin", "password", false);
                        } else {
                            ui_show("ADMIN FAIL", "TRY AGAIN");
                            record_event("admin_fail", "unknown", "password", false);
                            buzzer_beep(2, 80);

                            admin_password_mode = false;
                            ui_show("SMART DOOR", "INPUT PASSWORD");
                        }
                    } else {
                        /*
                         * 普通密码认证。
                         */
                        auth_submit_password(input_buf);

                        vTaskDelay(pdMS_TO_TICKS(500));

                        if (auth_system_is_locked()) {
                            ui_show("SYSTEM LOCKED", "PLEASE WAIT");
                        } else {
                            ui_show("SMART DOOR", "INPUT PASSWORD");
                        }
                    }

                    memset(input_buf, 0, sizeof(input_buf));
                    input_len = 0;
                }
            } else {
                ui_show("RESERVED KEY", "NO FUNCTION");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


/*
 *  RFID 任务
 */
static void rfid_task(void *arg)
{
    uint8_t uid[RFID_UID_MAX_LEN] = {0};
    uint8_t uid_len = 0;

    while (1) {
        memset(uid, 0, sizeof(uid));
        uid_len = 0;

        esp_err_t ret = rfid_mfrc522_read_card_uid(uid, &uid_len);

        if (ret == ESP_OK) {
            power_manager_feed_activity();

            auth_submit_card(uid, uid_len);

            vTaskDelay(pdMS_TO_TICKS(1000));

            if (!auth_system_is_locked()) {
                ui_show("SMART DOOR", "INPUT PASSWORD");
            }
        } else if (ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "RFID read failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(RFID_POLL_INTERVAL_MS));
    }
}


/*
 *  指纹任务
 */
static void fingerprint_task(void *arg)
{
    while (1) {
        uint16_t finger_id = 0;
        uint16_t confidence = 0;

        esp_err_t ret = fingerprint_as608_search(&finger_id, &confidence);

        if (ret == ESP_OK) {
            power_manager_feed_activity();

            auth_submit_fingerprint(finger_id, confidence);

            vTaskDelay(pdMS_TO_TICKS(1000));

            if (!auth_system_is_locked()) {
                ui_show("SMART DOOR", "INPUT PASSWORD");
            }
        } else if (ret != ESP_ERR_NOT_FOUND && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Fingerprint search failed: %s",
                     esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(FINGER_POLL_INTERVAL_MS));
    }
}



/*
 *  主入口
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Smart Access Door System Start");

    /*
     * 初始化 NVS。
     */
    ESP_ERROR_CHECK(app_nvs_init());

    /*
     * 初始化 OLED。
     * OLED 初始化失败不影响系统运行，仍可通过串口调试。
     */
    esp_err_t ui_ret = ui_init();
    if (ui_ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED init failed, serial log only");
    }

    ui_show("WIFI", "CONNECTING");

        //初始化摄像头
    esp_err_t camera_ret = camera_ov2640_init();
    if (camera_ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(camera_ret));
        ui_show("CAMERA FAIL", "FACE OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        ESP_LOGI(TAG, "Camera init OK");
        ui_show("CAMERA OK", "CAPTURE");
        camera_ov2640_capture_test();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // 临时禁用相机调试
    // esp_err_t camera_ret = ESP_FAIL;

    /*
     * 初始化 Wi-Fi。
     */
    esp_err_t wifi_ret = wifi_net_init();
    if (wifi_ret == ESP_OK) {
        wifi_ret = wifi_net_wait_connected(WIFI_CONNECT_TIMEOUT_MS);
    }

    if (wifi_ret == ESP_OK) {
        ui_show("WIFI OK", "SYNC TIME");

        /*
         * Wi-Fi 成功后进行时间同步。
         */
        esp_err_t time_ret = time_sync_init();
        if (time_ret != ESP_OK) {
            ESP_LOGW(TAG, "Time sync failed, continue with uptime");
            ui_show("TIME FAIL", "USE UPTIME");
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi unavailable, local mode");
        ui_show("WIFI FAIL", "LOCAL MODE");
    }
    //摄像头网页
    if (camera_ov2640_is_ready()) {
        camera_http_server_start();
        ui_show("CAMERA WEB OK", "OPEN IP");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else {
        ui_show("CAMERA WEB FALL","IP FALL");
    }
    
    /*
     * 初始化 MQTT。
     * 即使 Wi-Fi 当前失败，也可以先初始化 MQTT 客户端。
     */
    ESP_ERROR_CHECK(mqtt_client_app_init());

    if (wifi_ret == ESP_OK) {
        esp_err_t mqtt_ret = mqtt_client_app_start();

        if (mqtt_ret != ESP_OK) {
            ESP_LOGW(TAG, "MQTT start failed: %s",
                     esp_err_to_name(mqtt_ret));
        }
    }

    /*
     * 初始化事件日志。
     * 内部会初始化 CSV，并创建 MQTT 补传任务。
     */
    ESP_LOGI(TAG, "Before event_log_init, stack=%u",
         (unsigned int)uxTaskGetStackHighWaterMark(NULL));//确认栈残量
    ESP_ERROR_CHECK(event_log_init());
    ESP_LOGI(TAG, "After event_log_init, stack=%u",
         (unsigned int)uxTaskGetStackHighWaterMark(NULL));//确认栈残量



    /*
     * 初始化门禁基础模块。
     */
    ESP_ERROR_CHECK(alarm_init());
    ESP_ERROR_CHECK(lock_ctrl_init());
    esp_err_t keypad_ret = keypad_init();
    if (keypad_ret != ESP_OK) {
        ESP_LOGE(TAG,
                "Keypad init failed: %s",
                esp_err_to_name(keypad_ret));

        ui_show("KEYPAD FAIL", "CHECK PCF8574");
    } else {
        ESP_LOGI(TAG, "Keypad initialized");
    }
//建议以上改成更稳的写法，不要因为外设没接好就整机重启：

    ESP_ERROR_CHECK(auth_init());
    // 暂停人脸测试
    // ESP_ERROR_CHECK(face_auth_adapter_init());

    /*
     * 初始化 RFID。
     */
    esp_err_t rfid_ret = rfid_mfrc522_init();
    if (rfid_ret != ESP_OK) {
        ESP_LOGW(TAG, "RFID init failed, card disabled");
    }

    /*
     * 初始化 AS608 指纹模块。
     */
    esp_err_t finger_ret = fingerprint_as608_init();
    if (finger_ret != ESP_OK) {
        ESP_LOGW(TAG, "Fingerprint init failed, finger disabled");
    }



    /*
     * 初始化 TFLM 人脸识别模块。
     */
    // esp_err_t face_ret = ESP_FAIL;

    // if (camera_ret == ESP_OK) {
    //     face_ret = tflm_face_recognition_init();

    //     if (face_ret != ESP_OK) {
    //         ESP_LOGW(TAG, "TFLM face init failed");
    //     }
    // }

    /*
     * 初始化功耗管理。
     */
    ESP_ERROR_CHECK(power_manager_init());

    ui_show("SYSTEM READY", "INPUT PASSWORD");

    /*
     * 创建键盘任务。
     */
    //xTaskCreate(keypad_task, "keypad_task", 4096, NULL, 5, NULL);
    if (keypad_ret == ESP_OK) {
    xTaskCreate(keypad_task,"keypad_task",4096,NULL,5,NULL);
    }
    //添加判断
    /*
     * 创建 RFID 任务。
     */
    if (rfid_ret == ESP_OK) {
        xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 5, NULL);
    }

    /*
     * 创建指纹任务。
     */
    if (finger_ret == ESP_OK) {
        xTaskCreate(fingerprint_task, "fingerprint_task", 4096, NULL, 5, NULL);
    }

    /*
     * 启动 TFLM 人脸识别任务。
     */
    // if (face_ret == ESP_OK) {
    //     esp_err_t ret = tflm_face_recognition_start();

    //     if (ret != ESP_OK) {
    //         ESP_LOGW(TAG, "TFLM face start failed: %s",
    //                  esp_err_to_name(ret));
    //     }
    // }

    /*
     * 记录设备启动事件。
     * 该事件也会先写入 CSV，再通过 MQTT 上传。
     */
    record_event("device_start", "device", "system", false);
}