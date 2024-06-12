#include "wifi.h"
#include <string.h>
#include "common.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char* TAG = "WIFI";

#define SSID_KEY "WIFI_SSID"
#define SSID_LEN 32
#define PASS_KEY "WIFI_PASS"
#define PASS_LEN 64

#define WIFI_RESET_PIN  CONFIG_GPIO_WIFI_RESET
#define WIFI_RESET_MASK (1 << WIFI_RESET_PIN)

static EventGroupHandle_t event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

static const char nvs_namespace[] = "WIFI";

static esp_err_t nvs_get_value(nvs_handle_t handle, const char* key, uint8_t* value, size_t len) {
    esp_err_t find_key_result = nvs_find_key(handle, key, NULL);
    if (find_key_result != ESP_OK) {
        return find_key_result;
    }

    return nvs_get_str(handle, key, (char*)value, &len);
}

static esp_err_t nvs_set_value(nvs_handle_t handle, const char* key, uint8_t* value) {
    return nvs_set_str(handle, key, (const char*)value);
}

static esp_err_t reset_wifi_config(void) {
    // Configure button pin.
    gpio_config_t io_conf;
    io_conf.pin_bit_mask = WIFI_RESET_MASK;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_RETURN(gpio_config(&io_conf));

    // Read button state. Reset config on 0.
    bool reset_config = gpio_get_level(WIFI_RESET_PIN) == 0;
    if (reset_config) {
        nvs_handle_t nvs_handle;
        ESP_ERROR_RETURN(nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle));
        ESP_ERROR_RETURN(nvs_erase_all(nvs_handle));
        nvs_close(nvs_handle);
    }

    return ESP_OK;
}

static void smartconfig_task(void* arg) {
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    for (;;) {
        EventBits_t bits =
            xEventGroupWaitBits(event_group, ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if (bits & ESPTOUCH_DONE_BIT) {
            ESP_ERROR_CHECK(esp_smartconfig_stop());
            vTaskDelete(NULL);
        }
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                          void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGD(TAG, "STA started");
        // Try to connect.
        esp_err_t connect_result = esp_wifi_connect();
        if (connect_result == ESP_ERR_WIFI_SSID) {
            // If unable to connect - run SmartConfig task.
            // TODO: how to make it work alongside regular operation?
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 1, NULL);
        } else {
            ESP_ERROR_CHECK(connect_result);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGD(TAG, "STA disconnected");
        ESP_ERROR_CHECK(esp_wifi_connect());
        xEventGroupClearBits(event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGD(TAG, "STA connected - got IP");
        xEventGroupSetBits(event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGD(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;

        wifi_config_t wifi_cfg = {};
        memcpy(wifi_cfg.sta.ssid, evt->ssid, sizeof(wifi_cfg.sta.ssid));
        memcpy(wifi_cfg.sta.password, evt->password, sizeof(wifi_cfg.sta.password));

        // Store new SSID and password in NVS.
        {
            nvs_handle_t nvs_handle;
            ESP_ERROR_CHECK(nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle));
            ESP_ERROR_CHECK(nvs_set_value(nvs_handle, SSID_KEY, wifi_cfg.sta.ssid));
            ESP_ERROR_CHECK(nvs_set_value(nvs_handle, PASS_KEY, wifi_cfg.sta.password));
            ESP_ERROR_CHECK(nvs_commit(nvs_handle));
            nvs_close(nvs_handle);
        }

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        ESP_LOGI(TAG, "Sent SmartConfig ACK");
        xEventGroupSetBits(event_group, ESPTOUCH_DONE_BIT);
    }
}

esp_err_t wifi_init(void) {
    ESP_LOGI(TAG, "Starting Wi-Fi");

    // Reset configuration if button is pressed during init.
    ESP_ERROR_RETURN(reset_wifi_config());

    // Create event group.
    event_group = xEventGroupCreate();

    // Initialize TCP/IP stack.
    ESP_ERROR_RETURN(esp_netif_init());
    ESP_ERROR_RETURN(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi.
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_RETURN(esp_wifi_init(&wifi_init_cfg));

    // Set-up event handlers.
    ESP_ERROR_RETURN(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_RETURN(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_RETURN(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    // Load and set configuration, then start.
    wifi_config_t wifi_cfg = {};
    // Read SSID and password stored in NVS.
    {
        nvs_handle_t nvs_handle;
        // 'NVS_READWRITE' is used to allow creation of the namespace.
        ESP_ERROR_RETURN(nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle));
        // It's okay to fail here.
        nvs_get_value(nvs_handle, SSID_KEY, wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid));
        nvs_get_value(nvs_handle, PASS_KEY, wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password));
        nvs_close(nvs_handle);
    }

    ESP_ERROR_RETURN(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_RETURN(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_RETURN(esp_wifi_start());

    // Wait for connection.
    for (;;) {
        EventBits_t bits =
            xEventGroupWaitBits(event_group, CONNECTED_BIT, true, false, portMAX_DELAY);
        if (bits & CONNECTED_BIT) {
            ESP_LOGD(TAG, "Connected to AP");
            break;
        }
    }

    return ESP_OK;
}
