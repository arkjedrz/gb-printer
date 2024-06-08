#pragma once

#include "esp_err.h"

/// @brief  Initialize and start web server.
///         Wi-Fi must already be initialized.
/// @return Error code.
esp_err_t webserver_init(void);