#ifndef FINGERPRINT_AS608_H
#define FINGERPRINT_AS608_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t fingerprint_as608_init(void);

esp_err_t fingerprint_as608_search(uint16_t *finger_id,
                                   uint16_t *confidence);

esp_err_t fingerprint_as608_enroll(uint16_t finger_id);

esp_err_t fingerprint_as608_delete(uint16_t finger_id);

#endif /* FINGERPRINT_AS608_H */