#pragma once

#define UNUSED __attribute__((unused))

#define ESP_ERROR_RETURN(x)               \
    do {                                  \
        esp_err_t err_rc = (x);           \
        if (unlikely(err_rc != ESP_OK)) { \
            return err_rc;                \
        }                                 \
    } while (0)
