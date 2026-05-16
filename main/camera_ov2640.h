#ifndef CAMERA_OV2640_H
#define CAMERA_OV2640_H

#include <stdbool.h>
#include "esp_err.h"

/*
 *  OV2640 摄像头模块
 */

esp_err_t camera_ov2640_init(void);

esp_err_t camera_ov2640_capture_test(void);

bool camera_ov2640_is_ready(void);
#endif /* CAMERA_OV2640_H */