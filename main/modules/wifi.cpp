#include "wifi.h"
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

const char *g_ssid = "Tet"; // hardcoded ssid and password from test wifi
const char *g_password = "12341234";

int retry_num = 0;

static std::atomic<bool> shuttingDown{false};
// w = Wifi('f','f')
static ZZ::EventHandler wifiEventHandler{
    WIFI_EVENT,
    [](int32_t event, void *) -> void {
        switch (event) {
        case WIFI_EVENT_STA_START:
            echo("WIFI CONNECTING....");
            esp_wifi_connect();
            return;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (shuttingDown.load()) {
                echo("WiFi disconnected, but we are shutting down");
                return;
            }

            echo("WiFi lost connection. Retrying to connect...");
            // ImageDelivery::queueDisable();
            esp_wifi_connect();
            return;
        }
    },
};

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    echo("wifi event handler");
    if (event_id == WIFI_EVENT_STA_START) {
        printf("WIFI CONNECTING....\n");
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        printf("WiFi CONNECTED\n");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("WiFi lost connection\n");
        if (retry_num < 5) {
            esp_wifi_connect();
            retry_num++;
            printf("Retrying to Connect...\n");
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        printf("Wifi got IP...\n\n");
    }
}

Wifi_test::Wifi_test(const std::string ssid, const std::string password) : Module(wifi, "Wifi") {
    // Setup wifi for ESP32
    echo("Creating wifi module");
}

void Wifi_test::v1() {
    //                          s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config{};

    strcpy((char *)wifi_config.sta.ssid, g_ssid);
    strcpy((char *)wifi_config.sta.password, g_password);
    // echo("wifi config ssid: %s", wifi_config.sta.ssid);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_connect();
    echo("Connecting to WiFi");
}

void Wifi_test::v2() {
    esp_netif_t *netif{esp_netif_create_default_wifi_sta()};
    const char *hostname = "esp32";
    esp_netif_set_hostname(netif, hostname);

    wifi_init_config_t cfg WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    // wifiEventHandler.registerMainLoop();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    wifi_pmf_config_t pmf_conf{};
    pmf_conf.capable = true;
    pmf_conf.required = false;

    wifi_sta_config_t staConf{};

    assert(strlen(g_ssid) < sizeof(staConf.ssid));
    assert(strlen(g_password) < sizeof(staConf.password));
    std::memcpy(staConf.ssid, g_ssid, strlen(g_ssid));
    std::memcpy(staConf.password, g_password, strlen(g_password));
    staConf.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    staConf.pmf_cfg = pmf_conf;

    wifi_config_t wifi_conf{};
    wifi_conf.sta = staConf;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_conf);
    esp_wifi_start();
    esp_wifi_connect();
    echo("Connecting to WiFi");
}

void Wifi_test::v3() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    wifi_pmf_config_t pmf_conf{};
    pmf_conf.capable = true;
    pmf_conf.required = false;
    wifi_sta_config_t staConf{};

    strcpy((char *)staConf.ssid, g_ssid);
    strcpy((char *)staConf.password, g_password);
    // echo("wifi config ssid: %s", wifi_config.sta.ssid);

    staConf.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    staConf.pmf_cfg = pmf_conf;

    wifi_config_t wifi_conf{};
    wifi_conf.sta = staConf;

    // echo the whole wifi config
    echo("wifi config ssid: %s", wifi_conf.sta.ssid);
    echo("wifi config password: %s", wifi_conf.sta.password);
    echo("wifi config authmode: %d", wifi_conf.sta.threshold.authmode);
    echo("wifi config pmf_cfg capable: %d", wifi_conf.sta.pmf_cfg.capable);
    echo("wifi config pmf_cfg required: %d", wifi_conf.sta.pmf_cfg.required);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_conf);
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_connect();
    echo("Connecting to WiFi");
}

void Wifi_test::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "v1") {
        Module::expect(arguments, 0, string, string);
        this->v1();
    } else if (method_name == "v2") {
        Module::expect(arguments, 0, string, string);
        this->v2();
    } else if (method_name == "v3") {
        Module::expect(arguments, 0, string, string);
        this->v3();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0, string, string);
        this->shutdown();
    } else {
        echo("Method not found");
    }
}

void Wifi_test::shutdown() {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    esp_event_loop_delete_default();
    esp_netif_deinit();
}
