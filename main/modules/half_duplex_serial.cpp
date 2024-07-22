#include "half_duplex_serial.h"
#include <cstring>

#define RX_BUF_SIZE 1024

HalfDuplexSerial::HalfDuplexSerial(const std::string name,
                                   const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num)
    : Serial(name, rx_pin, tx_pin, baud_rate, uart_num) {}

void HalfDuplexSerial::send_data(uint8_t slave_id, const std::string &data) const {
    std::string message = std::to_string(slave_id) + ":" + data;
    uart_write_bytes(this->uart_num, message.c_str(), message.length());
}

std::string HalfDuplexSerial::read_from_uart(uint32_t timeout) const {
    uint8_t data[RX_BUF_SIZE];
    int len = uart_read_bytes(this->uart_num, data, RX_BUF_SIZE, timeout);
    if (len > 0) {
        data[len] = '\0';
        return std::string((char *)data);
    }
    return "";
}

void HalfDuplexSerial::check_for_commands(uint8_t slave_id, const std::function<void(const std::string &)> &handler) const {
    std::string data = read_from_uart();
    if (!data.empty()) {
        uint8_t received_id = data[0] - '0';
        if (received_id == slave_id) {
            std::string message = data.substr(2); // Skip the "ID:"
            handler(message);
        }
    }
}