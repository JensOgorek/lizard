#include "ota.h"
#include "esp_event.h"
#include "esp_log.h" //for showing logs
#include "esp_netif.h"
#include "esp_system.h" //esp_init funtions esp_err_t
#include "esp_wifi.h"
#include "esp_zeug/eventhandler.h"
#include "freertos/FreeRTOS.h" //for delay,mutexs,semphrs rtos operations
#include "nvs_flash.h"         //non volatile storage
#include "utils/uart.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdio.h> //for basic printf commands
#include <string>  //for handling strings

#include "esp_https_ota.h"
#include "esp_ota_ops.h"

namespace ota {

static RTC_NOINIT_ATTR bool check_hash_;
static RTC_NOINIT_ATTR std::string ssid_;
static RTC_NOINIT_ATTR std::string password_;
static RTC_NOINIT_ATTR std::string url_;

int retry_num_ = 0;
int max_retry_num_ = 10;

static size_t total_content_length = 0;
static size_t received_length = 0;

// core.ota("nope", "1337133713371337", "http://192.168.245.227:1111/ota/binary")

void log_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error in %s: %s\n", message, error_name);
    }
}

void log_error(esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s\n", error_name);
    }
}

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    return err == ESP_OK;
}

void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        echo("WIFI CONNECTING...\n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        echo("WiFi CONNECTED\n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        echo("WiFi lost connection\n");
        esp_err_t reconnect_result = esp_wifi_connect();
        if (reconnect_result != ESP_OK) {
            log_error("Reconnecting WiFi", reconnect_result);
        }
        if (++retry_num_ <= max_retry_num_) {
            echo("Retrying to Connect... (%d/%d)\n", retry_num_, max_retry_num_);
        } else {
            echo("Max retry attempts reached, stopping WiFi attempts.\n");
        }
        break;
    }
    case IP_EVENT_STA_GOT_IP:
        echo("WiFi got IP...\n");
        attempt(url_.c_str());
        break;
    default:
        echo("Unhandled WiFi Event: %d\n", event_id);
        break;
    }
}

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        echo("HTTP_EVENT_ERROR\n");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        echo("HTTP_EVENT_ON_CONNECTED\n");
        break;
    case HTTP_EVENT_HEADER_SENT:
        echo("HTTP_EVENT_HEADER_SENT\n");
        break;
    case HTTP_EVENT_ON_HEADER:
        echo("HTTP_EVENT_ON_HEADER\n");
        break;
    case HTTP_EVENT_ON_DATA:
        received_length += evt->data_len;
        if (total_content_length == 0) {
            total_content_length = strtoul(evt->header_value, NULL, 10);
            echo("Total content length: %d bytes\n", total_content_length);
        }
        if (evt->data_len > 0) {
            echo("Received data (%d bytes), Total received: %d bytes\n", evt->data_len, received_length);
            double percentage = (double)received_length / total_content_length * 100;
            echo("Progress: %.2f%%\n", percentage);
        }
        // Optionally, you can check here if received_length == total_content_length
        if (received_length == total_content_length) {
            echo("All content received. Total size: %d bytes\n", total_content_length);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        echo("HTTP_EVENT_ON_FINISH\n");
        // Reset the counters after a complete HTTP transaction
        received_length = 0;
        total_content_length = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        echo("HTTP_EVENT_DISCONNECTED\n");
        break;
    default:
        echo("Unhandled HTTP Event\n");
        break;
    }
    return ESP_OK;
}

void init(const std::string ssid, const std::string password, const std::string url) {
    ssid_ = ssid;
    password_ = password;
    url_ = url;
    if (is_wifi_connected()) {
        echo("Already connected to WiFi");
        attempt(url.c_str());
    } else {
        setup_wifi(ssid.c_str(), password.c_str());
    }
}

void setup_wifi(const char *ssid, const char *password) {
    log_error(esp_netif_init());
    log_error(esp_netif_init());
    log_error(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    log_error(esp_wifi_init(&cfg));
    log_error(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    log_error(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_config{};

    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);

    log_error(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    log_error(esp_wifi_start());
    log_error(esp_wifi_set_mode(WIFI_MODE_STA));
    log_error(esp_wifi_connect());
    echo("Connecting to WiFi");
}

void attempt(const char *url) {
    esp_http_client_config_t config{};
    esp_err_t err;
    config.skip_cert_common_name_check = true;
    config.keep_alive_enable = true;
    config.url = url;
    config.buffer_size = 1024;
    config.timeout_ms = 10 * 1000;
    config.event_handler = http_event_handler;

    echo("OTA Attempting to connect to %s", url);

    err = esp_https_ota(&config);
    if (err == ESP_OK) {
        check_hash_ = true;
        shutdown();
        echo("OTA Successful. Rebooting");
        esp_restart();
    } else {
        echo("OTA Failed. Error: %s", esp_err_to_name(err));
    }
}

void shutdown() {
    log_error(esp_wifi_disconnect());
    log_error(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
    log_error(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
    log_error(esp_wifi_stop());
    log_error(esp_wifi_deinit());
    // log_error(esp_netif_deinit()); // not supported yet
    echo("WiFi module shutdown");
}

void veryify() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_SW) {
        echo("Not deepsleep or software reset");
        check_hash_ = false;
        ssid_ = nullptr;
        password_ = nullptr;
        url_ = nullptr;
    } else {
        echo("deepsleep or software reset");
    }

    if (check_hash_) {
    }
}
} // namespace ota