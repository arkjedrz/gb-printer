#pragma once

#include "esp_err.h"

/// @brief  Initialize and start Wi-Fi.
///         NVS flash must already be initialized.
/// @return Error code.
esp_err_t wifi_init(void);
