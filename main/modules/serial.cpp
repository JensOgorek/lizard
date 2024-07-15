#include "serial.h"
#include "esp_ota_ops.h"
#include "esp_spi_flash.h"
#include "utils/uart.h"
#include <cstring>
#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 1024

#include <esp32_port.h>
#include <esp_loader.h>

Serial::Serial(const std::string name,
               const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Module(serial, name), rx_pin(rx_pin), tx_pin(tx_pin), baud_rate(baud_rate), uart_num(uart_num) {
    if (uart_is_driver_installed(uart_num)) {
        throw std::runtime_error("serial interface is already in use");
    }

    const uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = false,
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0);
}

void Serial::enable_line_detection() const {
    uart_enable_pattern_det_baud_intr(this->uart_num, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(this->uart_num, 100);
}

void Serial::deinstall() const {
    uart_driver_delete(this->uart_num);
    gpio_reset_pin(this->rx_pin);
    gpio_reset_pin(this->tx_pin);
    gpio_set_direction(this->rx_pin, GPIO_MODE_INPUT);
    gpio_set_direction(this->tx_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(this->rx_pin, GPIO_FLOATING);
    gpio_set_pull_mode(this->tx_pin, GPIO_FLOATING);
}

size_t Serial::write(const uint8_t byte) const {
    const char send = byte;
    uart_write_bytes(this->uart_num, &send, 1);
    return 1;
}

void Serial::write_checked_line(const char *message, const int length) const {
    char buffer[1024];
    strncpy(buffer, message, length);
    int len = check(buffer, length);
    buffer[len++] = '\n';
    uart_write_bytes(this->uart_num, buffer, len);
}

int Serial::available() const {
    if (!uart_is_driver_installed(this->uart_num)) {
        return 0;
    }
    size_t available;
    uart_get_buffered_data_len(this->uart_num, &available);
    return available;
}

bool Serial::has_buffered_lines() const {
    return uart_pattern_get_pos(this->uart_num) != -1;
}

void Serial::flush() const {
    uart_flush(this->uart_num);
}

int Serial::read(uint32_t timeout) const {
    uint8_t data = 0;
    const int length = uart_read_bytes(this->uart_num, &data, 1, timeout);
    return length > 0 ? data : -1;
}

int Serial::read_line(char *buffer) const {
    int pos = uart_pattern_pop_pos(this->uart_num);
    return pos >= 0 ? uart_read_bytes(this->uart_num, (uint8_t *)buffer, pos + 1, 0) : 0;
}

void Serial::clear() const {
    while (this->available()) {
        this->read();
    }
}

std::string Serial::get_output() const {
    if (!this->available()) {
        return "";
    }

    static char buffer[256];
    int byte;
    int pos = 0;
    while ((byte = this->read()) >= 0) {
        pos += std::sprintf(&buffer[pos], pos == 0 ? "%02x" : " %02x", byte);
    }
    return buffer;
}

static auto initConnection(const uart_port_t uart_num,
                           const gpio_num_t rx_pin,
                           const gpio_num_t tx_pin,
                           const uint32_t baud_rate,
                           const uint32_t block_size) -> bool {
    loader_esp32_config_t conf{};
    conf.baud_rate = baud_rate;
    conf.uart_port = uart_num;
    conf.uart_rx_pin = rx_pin;
    conf.uart_tx_pin = tx_pin;

    esp_loader_error_t status{loader_port_esp32_init(&conf)};
    echo("loader_port_esp32_init() -> %u", status);
    if (status != ESP_LOADER_SUCCESS) {
        echo("initializing communication pins");
        return false;
    }

    return true;
}

static auto connect() -> bool {
    esp_loader_connect_args_t args{};
    args.trials = 4;
    args.sync_timeout = 100;
    esp_loader_error_t status{esp_loader_connect(&args)};
    echo("esp_loader_connect() -> %u", status);

    if (status != ESP_LOADER_SUCCESS) {
        echo("connecting failed");
        return false;
    }
    return true;
}

void Serial::send_ota_wire() {
    deinstall();

    if (!initConnection(uart_num, rx_pin, tx_pin, 115200, 0x1000)) {
        echo("Failed to initialize connection");
        return;
    }

    // Deiniter deiniter{};

    echo("Connecting..");
    if (!connect()) {
        echo("Failed to connect");
        return;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        echo("Could not find running partition");
        // vTaskDelete(NULL);
        return;
    }

    uint8_t *data = (uint8_t *)malloc(1024);
    if (data == NULL) {
        echo("Failed to allocate memory");
        // vTaskDelete(NULL);
        return;
    }

    size_t bytes_read = 0;
    for (size_t offset = 0; offset < running_partition->size; offset += 1024) {
        size_t read_size = (offset + 1024 > running_partition->size) ? (running_partition->size - offset) : 1024;
        esp_err_t err = esp_partition_read(running_partition, offset, data, read_size);
        if (err != ESP_OK) {
            echo("esp_partition_read failed at offset 0x%x (0x%x)", offset, err);
            break;
        }
        uart_write_bytes(UART_NUM_1, (const char *)data, read_size); // Ensure UART_NUM_1 is correct
        bytes_read += read_size;
    }

    echo("Finished reading partition, total bytes read: %d", bytes_read);
    free(data);
    // vTaskDelete(NULL);
}

void Serial::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        for (auto const &argument : arguments) {
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("type mismatch at argument");
            }
            this->write(argument->evaluate_integer());
        }
    } else if (method_name == "read") {
        const std::string output = this->get_output();
        echo("%s %s", this->name.c_str(), output.c_str());
    } else if (method_name == "wire_ota") {
        Module::expect(arguments, 0);
        this->send_ota_wire();
    } else {
        Module::call(method_name, arguments);
    }
}
