#ifndef LOCK_CTRL_H
#define LOCK_CTRL_H

#include "esp_err.h"

/*
 *  门锁控制模块
 *
 * 支持：
 * 1. 舵机开锁；
 * 2. 继电器控制电磁锁。
 */

esp_err_t lock_ctrl_init(void);

void lock_ctrl_open(void);

#endif /* LOCK_CTRL_H */