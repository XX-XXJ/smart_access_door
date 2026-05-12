#ifndef UI_OLED_H
#define UI_OLED_H

#include "esp_err.h"

/*
 * ============================================================
 *  OLED 显示模块
 * ============================================================
 *
 * 当前版本统一使用旧版 I2C 驱动：
 *
 *      driver/i2c.h
 *
 * 原因：
 *      esp32-camera 的 SCCB 当前也使用旧版 I2C。
 *      为避免 driver_ng 与旧驱动冲突，工程中不要混用新版 I2C。
 */

esp_err_t ui_init(void);

void ui_show(const char *line1, const char *line2);

void ui_clear(void);

#endif /* UI_OLED_H */