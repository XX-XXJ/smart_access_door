#include "face_model_data.h"

/*
 * ============================================================
 *  模型数据占位文件
 * ============================================================
 *
 * 注意：
 * 这里不是有效模型，只是为了让工程结构完整。
 *
 * 训练得到 face_model_int8.tflite 后，使用：
 *
 *      xxd -i face_model_int8.tflite > face_model_data_raw.cc
 *
 * 然后把生成的数组名改成：
 *
 *      g_face_model_data
 *      g_face_model_data_len
 *
 * 再替换本文件内容。
 */

const unsigned char g_face_model_data[] = {
    0x20, 0x00, 0x00, 0x00
};

const unsigned int g_face_model_data_len = sizeof(g_face_model_data);