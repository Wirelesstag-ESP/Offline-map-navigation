#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float heading_deg;
    float pitch_deg;
    float roll_deg;
    uint8_t accuracy;
    bool accuracy_valid;
    bool valid;
    uint32_t last_update_ms;
} bno085_attitude_data_t;

esp_err_t bno085_attitude_init(void);
esp_err_t bno085_attitude_get(bno085_attitude_data_t *data);
esp_err_t bno085_attitude_set_heading_zero(void);

#ifdef __cplusplus
}
#endif
