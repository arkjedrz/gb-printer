#include "wifi.h"
#include <string.h>
#include "common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"

static const char* TAG = "WIFI";

// TODO: concat base SSID with device specific ID.
#define AP_SSID      CONFIG_AP_SSID
#define AP_PASS      CONFIG_AP_PASS
#define WIFI_CHANNEL CONFIG_WIFI_CHANNEL
#define MAX_STA_CONN CONFIG_MAX_STA_CONN

esp_err_t wifi_init(void) {
    ESP_LOGI(TAG, "Starting Wi-Fi");

    // Initialize TCP/IP stack.
    ESP_ERROR_RETURN(esp_netif_init());
    ESP_ERROR_RETURN(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // Initialize Wi-Fi.
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_RETURN(esp_wifi_init(&wifi_init_cfg));

    // Set configuration, then start.
    wifi_config_t wifi_cfg = {.ap = {
                                  .ssid = AP_SSID,
                                  .ssid_len = strlen(AP_SSID),
                                  .channel = WIFI_CHANNEL,
                                  .password = AP_PASS,
                                  .max_connection = MAX_STA_CONN,
                                  .authmode = WIFI_AUTH_WPA2_PSK,
                              }};

    ESP_ERROR_RETURN(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_RETURN(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_RETURN(esp_wifi_start());

    ESP_LOGI(TAG, "Access point initialized, SSID: %s, channel: %d", AP_SSID, WIFI_CHANNEL);
    return ESP_OK;
}
