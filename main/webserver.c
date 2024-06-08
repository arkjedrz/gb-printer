#include "webserver.h"
#include "common.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "image_builder.h"
#include "mdns.h"
#include "printer.h"

UNUSED static const char* TAG = "WEBSERVER";

static httpd_handle_t handle = NULL;

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

static esp_err_t get_status_handler(httpd_req_t* req) {
    char resp[512];
    sprintf(resp,
            "link_active = %d\n"
            "num_image_parts = %d\n"
            "image_ready = %d\n",
            printer_is_link_active(), image_num_parts(), image_png_ready());

    // Set content type.
    ESP_ERROR_RETURN(httpd_resp_set_type(req, "text/plain"));
    // Send data.
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static httpd_uri_t get_status_def = {
    .uri = "/status", .method = HTTP_GET, .handler = get_status_handler, .user_ctx = NULL};

static esp_err_t get_image_handler(httpd_req_t* req) {
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

static httpd_uri_t get_image_def = {
    .uri = "/image", .method = HTTP_GET, .handler = get_image_handler, .user_ctx = NULL};

static esp_err_t delete_image_handler(httpd_req_t* req) {
    image_png_clear();
    return httpd_resp_send(req, "", 0);
}

static httpd_uri_t delete_image_def = {.uri = "/delete-image",
                                       .method = HTTP_DELETE,
                                       .handler = delete_image_handler,
                                       .user_ctx = NULL};

static esp_err_t start_webserver(void) {
    // Start server.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_RETURN(httpd_start(&handle, &config));

    // Register handlers.
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &get_status_def));
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &get_image_def));
    ESP_ERROR_RETURN(httpd_register_uri_handler(handle, &delete_image_def));

    return ESP_OK;
}

esp_err_t webserver_init(void) {
    // Initialize mDNS and server.
    ESP_ERROR_RETURN(start_mdns());
    ESP_ERROR_RETURN(start_webserver());

    return ESP_OK;
}
