#include "printer.h"
#include <string.h>
#include "common.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "image_builder.h"

static const char* TAG = "PRINTER";

// Printer definitions.

#define DETECT_PIN    CONFIG_GPIO_DETECT
#define DETECT_MASK   (1 << DETECT_PIN)
#define TX_PIN        CONFIG_GPIO_TX
#define TX_MASK       (1 << TX_PIN)
#define RX_PIN        CONFIG_GPIO_RX
#define RX_MASK       (1 << RX_PIN)
#define CLOCK_PIN     CONFIG_GPIO_CLOCK
#define CLOCK_MASK    (1 << CLOCK_PIN)
#define MAX_DATA_SIZE 0x280

/// @brief Printer packet.
typedef struct {
    uint8_t command;
    uint8_t compression;
    uint16_t length;
    uint16_t received_checksum;
    uint16_t computed_checksum;
} Packet;

/// @brief Printer state.
typedef struct {
    // Processed bit index.
    uint8_t bit_counter;
    // Processed byte index.
    uint16_t byte_counter;
    // Packet is being read.
    bool is_reading_packet;
    // Current printer status.
    uint8_t status;

    // Input/output buffers.
    uint8_t rx_data_u8;
    uint16_t rx_data_u16;
    uint8_t tx_data_u8;
} Printer;

static SemaphoreHandle_t image_ready_semaphore;
static TimerHandle_t conn_timeout_timer;
static TimerHandle_t image_timeout_timer;
static Packet packet = {};
static Printer printer = {};
static ImageData image_data = {};

static void set_status(enum StatusMask mask) { printer.status |= mask; }

static void reset_status(enum StatusMask mask) { printer.status &= !mask; }

/// @brief Handle byte, once received.
///        Command specific operations are performed during handling of 'data' section.
static void process_byte() {
    // 'else if' is not used intentionally in this function.
    // This is to allow commands handling and checksum receiving.

    // Command.
    if (printer.byte_counter == 0) {
        packet.command = printer.rx_data_u8;
        packet.computed_checksum = printer.rx_data_u8;

        // Check if command is valid.
        switch (packet.command) {
            case 0x01:
            case 0x02:
            case 0x04:
            case 0x0F:
                break;
            default: {
                set_status(STATUS_PACKET_ERROR);
            }
        }
    }

    // Compression.
    if (printer.byte_counter == 1) {
        packet.compression = printer.rx_data_u8;
        packet.computed_checksum += printer.rx_data_u8;

        // Check if compression is expected - currently not supported.
        if (packet.compression > 0) {
            set_status(STATUS_OTHER_ERROR);
        }

        // Check if there's a processed image in the memory.
        if (image_png_buffer() != NULL) {
            set_status(STATUS_PAPER_JAM);
        }
    }

    // Data length low.
    if (printer.byte_counter == 2) {
        packet.length = printer.rx_data_u8 & 0xFF;
        packet.computed_checksum += printer.rx_data_u8;
    }

    // Data length high.
    if (printer.byte_counter == 3) {
        packet.length |= (printer.rx_data_u8 & 0xFF) << 8;
        packet.computed_checksum += printer.rx_data_u8;

        // Check if length is valid.
        bool length_valid = false;
        if (packet.command == 0x02) {
            length_valid = packet.length == 4;
        } else if (packet.command == 0x04) {
            length_valid = packet.length <= MAX_DATA_SIZE;
        } else {
            length_valid = packet.length == 0;
        }
        if (!length_valid) {
            set_status(STATUS_PACKET_ERROR);
        }
    }

    // Data and commands.
    if (packet.command == 0x01 || packet.command == 0x0F ||
        (printer.byte_counter >= 4 && printer.byte_counter < 4 + packet.length)) {
        // Received data index.
        uint16_t data_index = printer.byte_counter - 4;

        // Perform command specific operations.
        // This approach is used to ensure all the copying happens in-place.
        switch (packet.command) {
            // Initialize.
            case 0x01: {
                // Clear printer image data.
                image_data.number_of_sheets = 0;
                image_data.margins = 0;
                image_data.palette = 0;
                image_data.exposure = 0;
                image_data.length = 0;
                reset_status(STATUS_DATA_FULL);
                reset_status(STATUS_DATA_UNPROCESSED);
                break;
            }
            // Start printing.
            case 0x02: {
                // Read image data, then allow print task to handle printing.
                switch (data_index) {
                    case 0: {
                        image_data.number_of_sheets = printer.rx_data_u8;
                        break;
                    }
                    case 1: {
                        image_data.margins = printer.rx_data_u8;
                        break;
                    }
                    case 2: {
                        image_data.palette = printer.rx_data_u8;
                        break;
                    }
                    case 3: {
                        image_data.exposure = printer.rx_data_u8;
                        xSemaphoreGiveFromISR(image_ready_semaphore, NULL);
                        break;
                    }
                }
                packet.computed_checksum += printer.rx_data_u8;
                break;
            }
            // Fill buffer.
            case 0x04: {
                image_data.data[image_data.length] = printer.rx_data_u8;
                packet.computed_checksum += printer.rx_data_u8;
                ++image_data.length;

                // Check if image data is full.
                if (image_data.length == IMAGE_BUFFER_SIZE) {
                    set_status(STATUS_DATA_FULL);
                }

                break;
            }
            // Check status.
            case 0x0F: {
                // Unprocessed data flag is set here.
                // This is to avoid flag being raised once any data arrived.
                if (image_data.length > 0) {
                    set_status(STATUS_DATA_UNPROCESSED);
                }
                break;
            }
        }
    }

    // Checksum low.
    if (printer.byte_counter == 4 + packet.length) {
        packet.received_checksum = printer.rx_data_u8 & 0xFF;
    }

    // Checksum high.
    if (printer.byte_counter == 5 + packet.length) {
        packet.received_checksum |= (printer.rx_data_u8 & 0xFF) << 8;

        // Check if checksum is valid.
        if (packet.received_checksum != packet.computed_checksum) {
            set_status(STATUS_CHECKSUM_ERROR);
        }

        // Once checksum is received - always send '0x81'.
        printer.tx_data_u8 = 0x81;
    }

    // Printer status.
    if (printer.byte_counter == 6 + packet.length) {
        printer.tx_data_u8 = printer.status;
    }

    // Allow status to be sent.
    if (printer.byte_counter == 7 + packet.length) {
        // Reset 'byte_counter' and 'is_reading_packet'.
        printer.byte_counter = 0;
        printer.is_reading_packet = false;
        return;
    }

    ++printer.byte_counter;
}

static void IRAM_ATTR clock_isr_handler(UNUSED void* arg) {
    // Reset timeout timer.
    xTimerResetFromISR(conn_timeout_timer, NULL);
    xTimerResetFromISR(image_timeout_timer, NULL);

    // Read data.
    int rx_level = gpio_get_level(RX_PIN);
    printer.rx_data_u8 <<= 1;
    printer.rx_data_u8 |= rx_level & 0x01;
    printer.rx_data_u16 <<= 1;
    printer.rx_data_u16 |= rx_level & 0x01;

    // Initialize if sync word encountered and receiving yet.
    const uint16_t kSyncWord = 0x8833;
    if (!printer.is_reading_packet && printer.rx_data_u16 == kSyncWord) {
        printer.bit_counter = 0;
        printer.byte_counter = 0;
        printer.is_reading_packet = true;
        return;
    }

    // Process received byte.
    if (printer.is_reading_packet) {
        if (printer.bit_counter == 7) {
            process_byte();
            printer.bit_counter = 0;
        } else {
            ++printer.bit_counter;
        }
    }

    // Write data.
    // Data must be set before next rising edge.
    int tx_level = printer.tx_data_u8 & 0x80;
    printer.tx_data_u8 <<= 1;
    gpio_set_level(TX_PIN, tx_level);
}

static void process_image_task(UNUSED void* arg) {
    ESP_LOGD(TAG, "Image processing task started");
    for (;;) {
        if (!xSemaphoreTake(image_ready_semaphore, portMAX_DELAY)) {
            continue;
        }

        // Printing is active.
        set_status(STATUS_CURRENTLY_PRINTING);

        // Print image information.
        ESP_LOGV(TAG, "Image received");
        ESP_LOGV(TAG, "Sheets:   %02x", image_data.number_of_sheets);
        ESP_LOGV(TAG, "Margins:  %02x", image_data.margins);
        ESP_LOGV(TAG, "Palette:  %02x", image_data.palette);
        ESP_LOGV(TAG, "Exposure: %02x", image_data.exposure);
        ESP_LOGV(TAG, "Length:   %04x", image_data.length);
        ESP_LOGV(TAG, "Data:     %02x %02x %02x %02x...", image_data.data[0], image_data.data[1],
                 image_data.data[2], image_data.data[3]);

        // Add image data to image builder.
        ESP_ERROR_CHECK(image_add_data(&image_data));

        // Printing is not active.
        reset_status(STATUS_CURRENTLY_PRINTING);

        // Received data is now processed.
        reset_status(STATUS_DATA_UNPROCESSED);
    }
}

void conn_timeout_cb(UNUSED TimerHandle_t timer_handle) {
    ESP_LOGV(TAG, "Connection timeout");

    // Reset state of the printer.
    memset(&packet, 0, sizeof(Packet));
    memset(&printer, 0, sizeof(Printer));
}

void image_timeout_cb(UNUSED TimerHandle_t timer_handle) {
    ESP_LOGV(TAG, "Image timeout");

    // Skip if no image is available.
    if (image_num_parts() == 0) {
        return;
    }

    // Process available data to create an image.
    ESP_LOGI(TAG, "Image data is available - processing");
    ESP_ERROR_CHECK(image_process());

    // Reset state of the image.
    image_clear();
    memset(&image_data, 0, sizeof(ImageData));
}

esp_err_t printer_init(void) {
    ESP_LOGI(TAG, "Starting printer");
    gpio_config_t io_conf;

    // Configure output pin.
    ESP_LOGD(TAG, "Initializing Tx pin %d", TX_PIN);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = TX_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_RETURN(gpio_config(&io_conf));

    // Configure input pins.
    ESP_LOGD(TAG, "Initializing Rx pin %d", RX_PIN);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = RX_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_RETURN(gpio_config(&io_conf));

    ESP_LOGD(TAG, "Initializing clock pin %d", CLOCK_PIN);
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = CLOCK_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_RETURN(gpio_config(&io_conf));

    ESP_LOGD(TAG, "Initializing detect pin %d", DETECT_PIN);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = DETECT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_RETURN(gpio_config(&io_conf));

    // Create semaphores.
    image_ready_semaphore = xSemaphoreCreateBinary();

    // Create and start timers.
    const int kConnTimeoutTicks = pdMS_TO_TICKS(100);
    conn_timeout_timer =
        xTimerCreate("conn_timeout_timer", kConnTimeoutTicks, true, NULL, conn_timeout_cb);
    xTimerStart(conn_timeout_timer, kConnTimeoutTicks);
    const int kImageTimeoutTicks = pdMS_TO_TICKS(500);
    image_timeout_timer =
        xTimerCreate("image_timeout_timer", kImageTimeoutTicks, true, NULL, image_timeout_cb);
    xTimerStart(image_timeout_timer, kImageTimeoutTicks);

    // Start task for processing packets and images.
    ESP_LOGD(TAG, "Creating packet and image processing tasks");
    xTaskCreate(process_image_task, "process_image_task", 2048, NULL, 1, NULL);

    // Configure interrupt.
    ESP_LOGD(TAG, "Configuring clock pin interrupt");

    // Install GPIO ISR service.
    const int kEspIntrFlag = ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM;
    ESP_ERROR_RETURN(gpio_install_isr_service(kEspIntrFlag));

    // ISR handler for clock pin.
    ESP_ERROR_RETURN(gpio_isr_handler_add(CLOCK_PIN, clock_isr_handler, NULL));

    return ESP_OK;
}

bool printer_gb_connected(void) { return gpio_get_level(DETECT_PIN) > 0; }

uint8_t printer_status(void) { return printer.status; }