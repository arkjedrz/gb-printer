#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "printer.h"
#include "webserver.h"
#include "wifi.h"

static const char* TAG = "MAIN";

static SemaphoreHandle_t core0_initialized_semaphore = NULL;
static SemaphoreHandle_t core1_initialized_semaphore = NULL;

static void core0_task(UNUSED void* arg) {
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(webserver_init());
    xSemaphoreGive(core0_initialized_semaphore);
    for (;;) {
        // Prevent task from ending.
    }
}

static void core1_task(UNUSED void* arg) {
    ESP_ERROR_CHECK(printer_init());
    xSemaphoreGive(core1_initialized_semaphore);
    for (;;) {
        // Prevent task from ending.
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "GB PRINTER EMULATOR");

    // Create semaphores.
    core0_initialized_semaphore = xSemaphoreCreateBinary();
    core1_initialized_semaphore = xSemaphoreCreateBinary();

    // Initialize NVS.
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    ESP_ERROR_CHECK(res);

    // Initialize components.
    // TODO: optimize stack sizes.
    // Run Wi-Fi and web server on task assigned to core 0.
    xTaskCreatePinnedToCore(core0_task, "core0_task", 32 * 1024, NULL, 1, NULL, 0);
    // Run printer on task assigned to core 1.
    xTaskCreatePinnedToCore(core1_task, "core1_task", 16 * 1024, NULL, 5, NULL, 1);

    // Wait for components to initialize.
    ESP_ERROR_CHECK(xSemaphoreTake(core0_initialized_semaphore, portMAX_DELAY) ? ESP_OK
                                                                               : ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(xSemaphoreTake(core1_initialized_semaphore, portMAX_DELAY) ? ESP_OK
                                                                               : ESP_ERR_TIMEOUT);

    ESP_LOGI(TAG, "Device initialized");
    for (;;) {
        // Main processing loop.
    }
}
