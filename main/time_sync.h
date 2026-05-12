#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

/*
 *  时间同步模块
 *
 * 功能：
 * 1. 通过 SNTP 获取网络时间；
 * 2. 提供格式化时间字符串；
 * 3. 若未同步成功，则返回设备运行时间。
 */

esp_err_t time_sync_init(void);

bool time_sync_is_valid(void);

void time_sync_get_datetime(char *out, size_t out_size);

#endif /* TIME_SYNC_H */