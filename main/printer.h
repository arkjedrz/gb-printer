#pragma once

#include <stdbool.h>
#include "esp_err.h"

/// @brief  Initialize and start printer.
/// @return Error code.
esp_err_t printer_init(void);

/// @return True if communication between GB and this device is ongoing.
bool printer_is_link_active(void);