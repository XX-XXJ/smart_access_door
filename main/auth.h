#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 *  统一认证模块
 *
 * 所有认证方式都进入这里：
 * 1. 密码；
 * 2. 刷卡；
 * 3. 指纹；
 * 4. 人脸。
 */

esp_err_t auth_init(void);

void auth_submit_password(const char *password);

void auth_submit_card(const uint8_t *uid, uint8_t uid_len);

void auth_submit_fingerprint(uint16_t finger_id, uint16_t confidence);

void auth_submit_face_id(uint16_t face_id, int similarity);

bool auth_system_is_locked(void);

#endif /* AUTH_H */