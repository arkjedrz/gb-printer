#include "image_builder.h"
#include <string.h>
#include "common.h"
#include "esp_log.h"
#include "lodepng.h"

static const char* TAG = "IMAGE";

#define PALETTE_SIZE 4

// Fixed image width in pixels.
static const uint32_t px_width = 160;
// Fixed image width in tiles.
static const uint32_t tile_width = px_width / 8;

static int num_image_parts = 0;
static ImageData* image_parts = NULL;

static uint8_t* png_buffer = NULL;
static size_t png_length = 0;

void image_clear(void) {
    free(image_parts);
    image_parts = NULL;
    num_image_parts = 0;
}

esp_err_t image_add_data(ImageData* image_data) {
    int current_num = num_image_parts;
    // Increase number of entries.
    ++num_image_parts;
    // Increase size of memory.
    image_parts = realloc(image_parts, sizeof(ImageData) * num_image_parts);
    // Copy data.
    memcpy(image_parts + current_num, image_data, sizeof(ImageData));

    return ESP_OK;
}

int image_num_parts(void) { return num_image_parts; }

static uint32_t coord_1d(uint32_t x, uint32_t y, uint32_t width) { return y * width + x; }

static void draw_tile(uint8_t* buffer, ImageData* image_part, const uint8_t palette_lut[],
                      uint32_t x_tile, uint32_t y_tile, uint32_t y_tile_offset) {
    const uint32_t y_px_start = y_tile * 8;
    const uint32_t x_px_start = x_tile * 8;
    const uint32_t y_tile_compensated = y_tile - y_tile_offset;

    uint8_t* buf_ptr = image_part->data + (coord_1d(x_tile, y_tile_compensated, tile_width) * 16);

    for (uint32_t y_px = y_px_start; y_px < y_px_start + 8; ++y_px) {
        uint32_t x_px = x_px_start;
        const uint8_t low_byte = *buf_ptr;
        const uint8_t high_byte = *(buf_ptr + 1);
        for (int b = 7; b >= 0; --b) {
            const uint8_t low_value = (low_byte & (1 << b)) >> b;
            const uint8_t high_value = (high_byte & (1 << b)) >> b;
            const uint8_t color_id = high_value << 1 | low_value;
            const uint32_t coord = coord_1d(x_px, y_px, px_width);
            buffer[coord] = palette_lut[color_id];

            ++x_px;
        }
        buf_ptr += 2;
    }
}

static esp_err_t create_palette_lut(const ImageData* image_data, uint8_t palette_lut[]) {
    // Build 8-bit grayscale palette based on 2-bit GB palette.
    const uint8_t gb_palette = image_data->palette;
    // Exposure is 7-bit value - ignore MSB.
    const uint8_t exposure = image_data->exposure & 0x7F;
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        // Apply values based on GB palette.
        uint8_t gb_palette_value = (gb_palette & (0b11 << i * 2)) >> i * 2;
        uint8_t printer_palette_value = 0;
        switch (gb_palette_value) {
            case 0b11:
                printer_palette_value = 0x00;
                break;
            case 0b10:
                printer_palette_value = 0x40;
                break;
            case 0b01:
                printer_palette_value = 0xBF;
                break;
            case 0b00:
                printer_palette_value = 0xFF;
                break;
        }

        // Modify value based on exposure.
        // '0x00' - -25%
        // '0x40' - 0%
        // '0x7F' - +25%
        const float exposure_offset = exposure - 0x40;
        const int16_t lut_value = printer_palette_value + exposure_offset;
        if (lut_value > UINT8_MAX) {
            printer_palette_value = UINT8_MAX;
        } else if (lut_value < 0) {
            printer_palette_value = 0;
        } else {
            printer_palette_value = lut_value;
        }
        palette_lut[i] = printer_palette_value;
    }

    return ESP_OK;
}

static esp_err_t create_bitmap(uint8_t** buffer, uint32_t* image_height_px) {
    // TODO: remove/change arbitrary limitation to 32 image parts.
    if (num_image_parts > 32) {
        ESP_LOGE(TAG, "Unsupported number of image parts: %d", num_image_parts);
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate required sizes.
    size_t bitmap_length = 0;
    size_t num_tiles[32] = {0};
    for (size_t i = 0; i < num_image_parts; ++i) {
        const size_t length = image_parts[i].length;

        // Increase bitmap length, assuming 8bpp depth.
        bitmap_length += length * 4;

        // Increase height.
        // - each byte contains data for 4 pixels
        // - image width is fixed to 160 pixels
        // - each tile is 8 pixels tall
        const uint32_t local_height_px = (length * 4) / px_width;
        if (local_height_px != 0 && local_height_px % 8 != 0) {
            ESP_LOGE(TAG, "Image part height not divisable by 8: %lu", local_height_px);
            return ESP_ERR_INVALID_SIZE;
        }
        *image_height_px += local_height_px;
        num_tiles[i] = local_height_px / 8;
    }

    // Allocate bitmap buffer.
    *buffer = calloc(bitmap_length, sizeof(uint8_t));

    // Draw each tile.
    uint32_t curr_tile_height = 0;
    for (int i = 0; i < num_image_parts; ++i) {
        const uint32_t tile_height = num_tiles[i];
        ImageData* image_data = &image_parts[i];
        uint8_t palette_lut[PALETTE_SIZE];
        ESP_ERROR_RETURN(create_palette_lut(image_data, palette_lut));
        for (size_t y = curr_tile_height; y < curr_tile_height + tile_height; ++y) {
            for (size_t x = 0; x < tile_width; ++x) {
                draw_tile(*buffer, image_data, palette_lut, x, y, curr_tile_height);
            }
        }
        curr_tile_height += tile_height;
    }

    return ESP_OK;
}

esp_err_t image_process(void) {
    // Create a bitmap.
    uint8_t* bmp_buffer = NULL;
    uint32_t px_height = 0;
    esp_err_t bmp_result = create_bitmap(&bmp_buffer, &px_height);
    if (bmp_result != ESP_OK) {
        free(bmp_buffer);
        return bmp_result;
    }

    // Encode image to memory.
    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_GREY;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_GREY;
    state.info_png.color.bitdepth = 8;
    state.encoder.zlibsettings.btype = 0;
    unsigned int result =
        lodepng_encode(&png_buffer, &png_length, bmp_buffer, px_width, px_height, &state);
    lodepng_state_cleanup(&state);
    free(bmp_buffer);
    if (result != 0) {
        ESP_LOGE(TAG, "LodePNG failed with error code: %u", result);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image ready");
    return ESP_OK;
}

size_t image_png_length(void) { return png_length; }

const uint8_t* image_png_buffer(void) { return png_buffer; }

void image_png_clear(void) {
    free(png_buffer);
    png_buffer = NULL;
    png_length = 0;
}