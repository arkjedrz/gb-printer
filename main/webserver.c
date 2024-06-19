#include "webserver.h"
#include <sys/stat.h>
#include "common.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "image_builder.h"
#include "mdns.h"
#include "printer.h"

static const char* TAG = "WEBSERVER";

static httpd_handle_t handle = NULL;
static const char* index_html_path = "/spiffs/index.html";
static char index_html_data[8 * 1024];

static esp_err_t start_spiffs(void) {
    // Initialize SPIFFS.
    esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                  .partition_label = NULL,
                                  .max_files = 5,
                                  .format_if_mount_failed = true};
    ESP_ERROR_RETURN(esp_vfs_spiffs_register(&conf));

    // Check if 'index.html' is available.
    struct stat st;
    if (stat(index_html_path, &st)) {
        return ESP_ERR_NOT_FOUND;
    }

    // Load page to memory.
    memset(index_html_data, 0, sizeof(index_html_data));
    FILE* index_file = fopen(index_html_path, "r");
    if (fread(index_html_data, st.st_size, 1, index_file) == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    fclose(index_file);

    return ESP_OK;
}

static esp_err_t start_mdns(void) {
    // Initialize and set names.
    ESP_ERROR_RETURN(mdns_init());

    // TODO: what will happen if multiple devices with same hostname exist?
    ESP_ERROR_RETURN(mdns_hostname_set("gb-printer"));
    ESP_ERROR_RETURN(mdns_instance_name_set("GB Printer emulator"));

    // Configure HTTP service.
    ESP_ERROR_RETURN(mdns_service_add("GB Printer emulator", "_http", "_tcp", 80, NULL, 0));

    return ESP_OK;
}

static esp_err_t main_page_get_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "main_page_get_handler");
    return httpd_resp_send(req, index_html_data, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t gb_connected_get_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "gb_connected_get_handler");
    char resp[16];
    sprintf(resp, "%d", printer_gb_connected());
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t printer_status_get_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "printer_status_get_handler");
    char resp[16];
    sprintf(resp, "%d", printer_status());
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t image_ready_get_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "image_ready_get_handler");
    char resp[16];
    sprintf(resp, "%d", image_png_buffer() != NULL);
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t image_get_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "image_get_handler");
    const size_t png_length = image_png_length();
    const uint8_t* png_buffer = image_png_buffer();

    // Image not ready, respond with 404.
    if (png_length == 0 || png_buffer == NULL) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Image not ready");
    }

    // Set content type.
    ESP_ERROR_RETURN(httpd_resp_set_type(req, "image/png"));
    // Send data.
    return httpd_resp_send(req, (const char*)png_buffer, png_length);
}

static esp_err_t image_delete_handler(httpd_req_t* req) {
    ESP_LOGV(TAG, "image_delete_handler");
    image_png_clear();
    return httpd_resp_send(req, "1", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_webserver(void) {
    // Start server.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_RETURN(httpd_start(&handle, &config));

    // Register handlers.

    const httpd_uri_t main_page_get = {
        .uri = "/", .method = HTTP_GET, .handler = main_page_get_handler, .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &main_page_get));

    const httpd_uri_t gb_connected_get = {.uri = "/gb-connected",
                                          .method = HTTP_GET,
                                          .handler = gb_connected_get_handler,
                                          .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &gb_connected_get));

    const httpd_uri_t printer_status_get = {.uri = "/printer-status",
                                            .method = HTTP_GET,
                                            .handler = printer_status_get_handler,
                                            .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &printer_status_get));

    const httpd_uri_t image_ready_get = {.uri = "/image-ready",
                                         .method = HTTP_GET,
                                         .handler = image_ready_get_handler,
                                         .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &image_ready_get));

    const httpd_uri_t image_get = {
        .uri = "/image", .method = HTTP_GET, .handler = image_get_handler, .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &image_get));

    const httpd_uri_t image_delete = {.uri = "/delete-image",
                                      .method = HTTP_DELETE,
                                      .handler = image_delete_handler,
                                      .user_ctx = NULL};
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &image_delete));

    return ESP_OK;
}

esp_err_t webserver_init(void) {
    // Initialize SPIFFS and load main page to memory.
    ESP_ERROR_RETURN(start_spiffs());

    // Initialize mDNS and server.
    ESP_ERROR_RETURN(start_mdns());
    ESP_ERROR_RETURN(start_webserver());

    return ESP_OK;
}
