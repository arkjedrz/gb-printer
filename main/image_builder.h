#pragma once

#include <stdbool.h>
#include "esp_err.h"

/// Buffer size for a single image.
#define IMAGE_BUFFER_SIZE 0x2000

/// @brief Single image data.
typedef struct {
    // Image parameters.
    uint8_t number_of_sheets;
    uint8_t margins;
    uint8_t palette;
    uint8_t exposure;

    // Data and length.
    uint16_t length;
    uint8_t data[IMAGE_BUFFER_SIZE];
} ImageData;

/// @brief  Remove stored image data.
void image_clear(void);

/// @brief              Add image data.
/// @param image_data   Data to be added. Data will be copied.
/// @return             Error code.
esp_err_t image_add_data(ImageData* image_data);

/// @return Number of stored image parts.
int image_num_parts(void);

/// @brief  Process image data to create an image.
///         Removes unprocessed data.
/// @return Error code.
esp_err_t image_process(void);

/// @return True if image is ready.
bool image_png_ready(void);

/// @brief  Get length of image buffer.
///         Image must be ready.
/// @return Length of PNG image buffer.
///         0 if not ready.
size_t image_png_length(void);

/// @brief  Get pointer to image buffer.
/// @return Pointer to PNG image buffer.
///         NULL if not ready.
const uint8_t* image_png_buffer(void);

/// @brief  Clear PNG buffer and reset PNG buffer length.
void image_png_clear(void);