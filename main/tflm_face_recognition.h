#ifndef TFLM_FACE_RECOGNITION_H
#define TFLM_FACE_RECOGNITION_H

#include "esp_err.h"

/*
 *  TFLM 人脸识别模块
 *
 * 功能：
 * 1. 加载 int8 量化 CNN 模型；
 * 2. 获取 OV2640 图像；
 * 3. 预处理为 96×96×1；
 * 4. TFLM 本地推理；
 * 5. 输出 face_id。
 */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tflm_face_recognition_init(void);

esp_err_t tflm_face_recognition_run_once(void);

esp_err_t tflm_face_recognition_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TFLM_FACE_RECOGNITION_H */