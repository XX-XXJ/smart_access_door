#ifndef RFID_MFRC522_H
#define RFID_MFRC522_H

#include <stdint.h>

#include "esp_err.h"

#define RFID_UID_MAX_LEN            10

esp_err_t rfid_mfrc522_init(void);

esp_err_t rfid_mfrc522_read_card_uid(uint8_t *uid, uint8_t *uid_len);

#endif /* RFID_MFRC522_H */