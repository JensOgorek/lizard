#include "core.h"
#include "../global.h"
#include "../storage.h"
#include "../utils/ota.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "driver/uart.h"
#include "esp_ota_ops.h"
#include "esp_spi_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <memory>
#include <stdlib.h>

#define BUF_SIZE (1024)

Core::Core(const std::string name) : Module(core, name) {
    this->properties["debug"] = std::make_shared<BooleanVariable>(false);
    this->properties["millis"] = std::make_shared<IntegerVariable>();
    this->properties["heap"] = std::make_shared<IntegerVariable>();
    this->properties["last_message_age"] = std::make_shared<IntegerVariable>();
}

void Core::step() {
    this->properties.at("millis")->integer_value = millis();
    this->properties.at("heap")->integer_value = xPortGetFreeHeapSize();
    this->properties.at("last_message_age")->integer_value = millis_since(this->last_message_millis);
    Module::step();
}

void Core::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "restart") {
        Module::expect(arguments, 0);
        esp_restart();
    } else if (method_name == "version") {
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        echo("version: %s", app_desc->version);
    } else if (method_name == "info") {
        Module::expect(arguments, 0);
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        echo("lizard version: %s", app_desc->version);
        echo("compile time: %s, %s", app_desc->date, app_desc->time);
        echo("idf version: %s", app_desc->idf_ver);
    } else if (method_name == "print") {
        static char buffer[1024];
        int pos = 0;
        for (auto const &argument : arguments) {
            if (argument != arguments[0]) {
                pos += sprintf(&buffer[pos], " ");
            }
            pos += argument->print_to_buffer(&buffer[pos]);
        }
        echo(buffer);
    } else if (method_name == "output") {
        Module::expect(arguments, 1, string);
        this->output_list.clear();
        std::string format = arguments[0]->evaluate_string();
        while (!format.empty()) {
            std::string element = cut_first_word(format);
            if (element.find('.') == std::string::npos) {
                // variable[:precision]
                std::string variable_name = cut_first_word(element, ':');
                const unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
                this->output_list.push_back({nullptr, variable_name, precision});
            } else {
                // module.property[:precision]
                std::string module_name = cut_first_word(element, '.');
                const ConstModule_ptr module = Global::get_module(module_name);
                const std::string property_name = cut_first_word(element, ':');
                const unsigned int precision = element.empty() ? 0 : atoi(element.c_str());
                this->output_list.push_back({module, property_name, precision});
            }
        }
        this->output_on = true;
    } else if (method_name == "startup_checksum") {
        uint16_t checksum = 0;
        for (char const &c : Storage::startup) {
            checksum += c;
        }
        echo("checksum: %04x", checksum);
    } else if (method_name == "ota") {
        Module::expect(arguments, 3, string, string, string);
        auto *params = new ota::ota_params_t{
            arguments[0]->evaluate_string(),
            arguments[1]->evaluate_string(),
            arguments[2]->evaluate_string(),
        };
        xTaskCreate(ota::ota_task, "ota_task", 8192, params, 5, nullptr);
    } else if (method_name == "receive_ota") {
        Module::expect(arguments, 0);
        xTaskCreate(this->receive_ota_uart, "uart_ota_task", 4096, NULL, 5, NULL);
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Core::get_output() const {
    static char output_buffer[1024];
    int pos = 0;
    for (auto const &element : this->output_list) {
        if (pos > 0) {
            pos += sprintf(&output_buffer[pos], " ");
        }
        const Variable_ptr variable =
            element.module ? element.module->get_property(element.property_name) : Global::get_variable(element.property_name);
        switch (variable->type) {
        case boolean:
            pos += sprintf(&output_buffer[pos], "%s", variable->boolean_value ? "true" : "false");
            break;
        case integer:
            pos += sprintf(&output_buffer[pos], "%lld", variable->integer_value);
            break;
        case number:
            pos += sprintf(&output_buffer[pos], "%.*f", element.precision, variable->number_value);
            break;
        case string:
            pos += sprintf(&output_buffer[pos], "\"%s\"", variable->string_value.c_str());
            break;
        default:
            throw std::runtime_error("invalid type");
        }
    }
    return std::string(output_buffer);
}
void Core::receive_ota_uart(void *pvParameters) {
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t update_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        echo("esp_ota_begin failed: 0x%x", err);
        vTaskDelete(NULL);
        return;
    }

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    if (data == NULL) {
        echo("Failed to allocate memory");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_received = 0;
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, BUF_SIZE, 20 / portTICK_RATE_MS);
        if (len > 0) {
            err = esp_ota_write(update_handle, (const void *)data, len);
            if (err != ESP_OK) {
                echo("esp_ota_write failed: 0x%x", err);
                break;
            }
            bytes_received += len;
        } else if (len < 0) {
            echo("Error reading data: 0x%x", len);
            break;
        }
    }

    err = esp_ota_end(update_handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(update_partition);
        if (err == ESP_OK) {
            echo("OTA succeeded, %d bytes received", bytes_received);
            esp_restart();
        } else {
            echo("esp_ota_set_boot_partition failed: 0x%x", err);
        }
    } else {
        echo("esp_ota_end failed: 0x%x", err);
    }

    echo("Total bytes received: %d", bytes_received);
    free(data);
    vTaskDelete(NULL);
}
void Core::keep_alive() {
    this->last_message_millis = millis();
}