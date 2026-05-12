#include "user_db.h"
#include "app_config.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "USER_DB";

#define USER_DB_NAMESPACE           "user_db"
#define USER_DB_KEY                 "users"

static user_record_t s_users[USER_DB_MAX_USERS];


/*
 *  保存用户表到 NVS
 */
static esp_err_t user_db_save(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(USER_DB_NAMESPACE,
                             NVS_READWRITE,
                             &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle,
                       USER_DB_KEY,
                       s_users,
                       sizeof(s_users));

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "User database saved");
    }

    return ret;
}


/*
 *  从 NVS 读取用户表
 */
static esp_err_t user_db_load(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(USER_DB_NAMESPACE,
                             NVS_READWRITE,
                             &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t required_size = sizeof(s_users);

    ret = nvs_get_blob(handle,
                       USER_DB_KEY,
                       s_users,
                       &required_size);

    nvs_close(handle);

    if (ret == ESP_OK && required_size == sizeof(s_users)) {
        ESP_LOGI(TAG, "User database loaded from NVS");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}


static int find_empty_slot(void)
{
    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used) {
            return i;
        }
    }

    return -1;
}


static int find_index_by_user_id(const char *user_id)
{
    if (user_id == NULL) {
        return -1;
    }

    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used) {
            continue;
        }

        if (strcmp(s_users[i].user_id, user_id) == 0) {
            return i;
        }
    }

    return -1;
}


/*
 *  恢复默认用户表
 */
esp_err_t user_db_reset_default(void)
{
    memset(s_users, 0, sizeof(s_users));

    user_record_t user001 = {
        .used = true,
        .user_id = "user001",
        .name = "XJ",
        .password = NORMAL_PASSWORD,
        .card_uid = {0x67, 0x98, 0xBA, 0x06},
        .card_uid_len = 4,
        .finger_id = 1,
        .has_finger = true,
        .face_id = 1,
        .has_face = true,
        .is_admin = false,
    };

    user_record_t admin = {
        .used = true,
        .user_id = "admin",
        .name = "Admin",
        .password = ADMIN_PASSWORD,
        .card_uid = {0xF2,0xFB,0xEF,0x06},
        .card_uid_len = 0,
        .finger_id = 0,
        .has_finger = false,
        .face_id = 0,
        .has_face = false,
        .is_admin = true,
    };

    s_users[0] = user001;
    s_users[1] = admin;

    ESP_LOGI(TAG, "Default user database created");

    return user_db_save();
}


esp_err_t user_db_init(void)
{
    esp_err_t ret = user_db_load();

    if (ret != ESP_OK) {
        ret = user_db_reset_default();
    }

    user_db_print_all();

    return ret;
}


esp_err_t user_db_add_or_update(const user_record_t *user)
{
    if (user == NULL || strlen(user->user_id) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int index = find_index_by_user_id(user->user_id);

    if (index < 0) {
        index = find_empty_slot();
        if (index < 0) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_users[index] = *user;
    s_users[index].used = true;

    return user_db_save();
}


esp_err_t user_db_delete_by_user_id(const char *user_id)
{
    int index = find_index_by_user_id(user_id);

    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_users[index].is_admin) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_users[index], 0, sizeof(user_record_t));

    return user_db_save();
}


esp_err_t user_db_bind_card(const char *user_id,
                            const uint8_t *uid,
                            uint8_t uid_len)
{
    if (uid == NULL || uid_len == 0 || uid_len > USER_DB_MAX_UID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    int index = find_index_by_user_id(user_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(s_users[index].card_uid, uid, uid_len);
    s_users[index].card_uid_len = uid_len;

    return user_db_save();
}


esp_err_t user_db_bind_finger(const char *user_id,
                              uint16_t finger_id)
{
    int index = find_index_by_user_id(user_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_users[index].finger_id = finger_id;
    s_users[index].has_finger = true;

    return user_db_save();
}


esp_err_t user_db_unbind_finger(const char *user_id)
{
    int index = find_index_by_user_id(user_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_users[index].finger_id = 0;
    s_users[index].has_finger = false;

    return user_db_save();
}


esp_err_t user_db_bind_face(const char *user_id,
                            uint16_t face_id)
{
    int index = find_index_by_user_id(user_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_users[index].face_id = face_id;
    s_users[index].has_face = true;

    return user_db_save();
}


esp_err_t user_db_unbind_face(const char *user_id)
{
    int index = find_index_by_user_id(user_id);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    s_users[index].face_id = 0;
    s_users[index].has_face = false;

    return user_db_save();
}


const user_record_t *user_db_find_by_user_id(const char *user_id)
{
    int index = find_index_by_user_id(user_id);

    if (index < 0) {
        return NULL;
    }

    return &s_users[index];
}


const user_record_t *user_db_find_by_password(const char *password)
{
    if (password == NULL) {
        return NULL;
    }

    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used) {
            continue;
        }

        if (strcmp(s_users[i].password, password) == 0) {
            return &s_users[i];
        }
    }

    return NULL;
}


const user_record_t *user_db_find_by_card_uid(const uint8_t *uid,
                                              uint8_t uid_len)
{
    if (uid == NULL || uid_len == 0) {
        return NULL;
    }

    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used) {
            continue;
        }

        if (s_users[i].card_uid_len != uid_len) {
            continue;
        }

        if (memcmp(s_users[i].card_uid, uid, uid_len) == 0) {
            return &s_users[i];
        }
    }

    return NULL;
}


const user_record_t *user_db_find_by_finger_id(uint16_t finger_id)
{
    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used || !s_users[i].has_finger) {
            continue;
        }

        if (s_users[i].finger_id == finger_id) {
            return &s_users[i];
        }
    }

    return NULL;
}


const user_record_t *user_db_find_by_face_id(uint16_t face_id)
{
    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used || !s_users[i].has_face) {
            continue;
        }

        if (s_users[i].face_id == face_id) {
            return &s_users[i];
        }
    }

    return NULL;
}


bool user_db_is_duress_password(const char *password)
{
    if (password == NULL) {
        return false;
    }

    return strcmp(password, DURESS_PASSWORD) == 0;
}


bool user_db_is_admin_password(const char *password)
{
    const user_record_t *user = user_db_find_by_password(password);

    if (user == NULL) {
        return false;
    }

    return user->is_admin;
}


void user_db_uid_to_string(const uint8_t *uid,
                           uint8_t uid_len,
                           char *out,
                           size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (uid == NULL || uid_len == 0) {
        snprintf(out, out_size, "EMPTY");
        return;
    }

    size_t pos = 0;

    for (uint8_t i = 0; i < uid_len; i++) {
        int written = snprintf(out + pos,
                               out_size - pos,
                               "%02X%s",
                               uid[i],
                               (i + 1 < uid_len) ? ":" : "");

        if (written < 0) {
            break;
        }

        pos += written;

        if (pos >= out_size) {
            break;
        }
    }
}


void user_db_print_all(void)
{
    ESP_LOGI(TAG, "========== USER LIST ==========");

    for (int i = 0; i < USER_DB_MAX_USERS; i++) {
        if (!s_users[i].used) {
            continue;
        }

        char uid_str[40] = {0};

        user_db_uid_to_string(s_users[i].card_uid,
                              s_users[i].card_uid_len,
                              uid_str,
                              sizeof(uid_str));

        ESP_LOGI(TAG,
                 "[%d] id=%s, name=%s, admin=%s, card=%s, finger=%s:%u, face=%s:%u",
                 i,
                 s_users[i].user_id,
                 s_users[i].name,
                 s_users[i].is_admin ? "true" : "false",
                 uid_str,
                 s_users[i].has_finger ? "true" : "false",
                 s_users[i].finger_id,
                 s_users[i].has_face ? "true" : "false",
                 s_users[i].face_id);
    }

    ESP_LOGI(TAG, "================================");
}