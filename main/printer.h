#pragma once

#include <stdbool.h>
#include "esp_err.h"

/// @brief Status masks.
enum StatusMask {
    /// @brief Invalid packet checksum.
    STATUS_CHECKSUM_ERROR = 1 << 0,
    /// @brief Currently printing/copying data to image builder.
    STATUS_CURRENTLY_PRINTING = 1 << 1,
    /// @brief Image data full.
    STATUS_DATA_FULL = 1 << 2,
    /// @brief Unprocessed data available in memory.
    STATUS_DATA_UNPROCESSED = 1 << 3,
    /// @brief Packet error. Set on invalid command or invalid length.
    STATUS_PACKET_ERROR = 1 << 4,
    /// @brief Paper jam. Set on output image still in memory.
    STATUS_PAPER_JAM = 1 << 5,
    /// @brief Other error. Set on unsupported features.
    STATUS_OTHER_ERROR = 1 << 6,
    /// @brief Low battery error. Never set.
    STATUS_LOW_BATTERY = 1 << 7
};

/// @brief  Initialize and start printer.
/// @return Error code.
esp_err_t printer_init(void);

/// @return True if communication between GB and this device is ongoing.
bool printer_is_link_active(void);

/// @return Current printer status. Use 'StatusMask' enum to decode.
uint8_t printer_status(void);