#ifndef FACE_AUTH_ADAPTER_H
#define FACE_AUTH_ADAPTER_H

#include <stdint.h>

#include "esp_err.h"

/*
 * ============================================================
 *  人脸识别适配层
 * ============================================================
 *
 * 作用：
 * 1. 接收 TFLM 人脸识别模块输出的 face_id；
 * 2. 将 face_id 转交给统一认证模块 auth；
 * 3. 对识别成功和未知人脸做冷却处理，避免重复开锁或重复记录。
 *
 * 注意：
 * tflm_face_recognition.cpp 是 C++ 文件，
 * face_auth_adapter.c 是 C 文件。
 *
 * 因此这里必须加 extern "C"，
 * 否则 C++ 文件调用 C 函数时会发生链接错误。
 */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t face_auth_adapter_init(void);

void face_auth_on_recognized(uint16_t face_id, int similarity);

void face_auth_on_unknown(void);

#ifdef __cplusplus
}
#endif

#endif /* FACE_AUTH_ADAPTER_H */