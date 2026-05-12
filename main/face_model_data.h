#ifndef FACE_MODEL_DATA_H
#define FACE_MODEL_DATA_H

#include <stddef.h>
#include <stdint.h>

/*
 *  TFLite 模型数据
 *
 * g_face_model_data：
 *      int8 量化后的轻量 CNN 模型数组。
 *
 * g_face_model_data_len：
 *      模型数组长度。
 */

extern const unsigned char g_face_model_data[];
extern const unsigned int g_face_model_data_len;

#endif /* FACE_MODEL_DATA_H */