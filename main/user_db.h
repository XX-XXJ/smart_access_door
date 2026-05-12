#ifndef USER_DB_H
#define USER_DB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 *  本地用户数据库模块
 *
 * 使用 NVS 保存用户信息。
 */

#define USER_DB_MAX_USERS           10
#define USER_DB_MAX_UID_LEN         10
#define USER_DB_USER_ID_LEN         16
#define USER_DB_NAME_LEN            32
#define USER_DB_PASSWORD_LEN        16

typedef struct {
    bool used;

    char user_id[USER_DB_USER_ID_LEN];
    char name[USER_DB_NAME_LEN];
    char password[USER_DB_PASSWORD_LEN];

    uint8_t card_uid[USER_DB_MAX_UID_LEN];
    uint8_t card_uid_len;

    uint16_t finger_id;
    bool has_finger;

    uint16_t face_id;
    bool has_face;

    bool is_admin;
} user_record_t;

esp_err_t user_db_init(void);

esp_err_t user_db_reset_default(void);

esp_err_t user_db_add_or_update(const user_record_t *user);

esp_err_t user_db_delete_by_user_id(const char *user_id);

esp_err_t user_db_bind_card(const char *user_id,
                            const uint8_t *uid,
                            uint8_t uid_len);

esp_err_t user_db_bind_finger(const char *user_id,
                              uint16_t finger_id);

esp_err_t user_db_unbind_finger(const char *user_id);

esp_err_t user_db_bind_face(const char *user_id,
                            uint16_t face_id);

esp_err_t user_db_unbind_face(const char *user_id);

const user_record_t *user_db_find_by_password(const char *password);

const user_record_t *user_db_find_by_card_uid(const uint8_t *uid,
                                              uint8_t uid_len);

const user_record_t *user_db_find_by_finger_id(uint16_t finger_id);

const user_record_t *user_db_find_by_face_id(uint16_t face_id);

const user_record_t *user_db_find_by_user_id(const char *user_id);

bool user_db_is_duress_password(const char *password);

bool user_db_is_admin_password(const char *password);

void user_db_uid_to_string(const uint8_t *uid,
                           uint8_t uid_len,
                           char *out,
                           size_t out_size);

void user_db_print_all(void);

#endif /* USER_DB_H */