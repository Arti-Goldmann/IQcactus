#pragma once

#include <stdbool.h>
#include <stdint.h>

// Подключиться к Wi-Fi (блокирует до получения IP или таймаута).
// Возвращает true при успехе.
bool wifi_connect(void);

// Запустить SNTP-синхронизацию времени (вызывать после wifi_connect).
void sntp_sync_start(void);

// Дождаться синхронизации времени (блокирует не более timeout_ms).
// Возвращает true если время получено.
bool sntp_wait_sync(uint32_t timeout_ms);
