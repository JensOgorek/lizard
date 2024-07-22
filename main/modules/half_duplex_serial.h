#pragma once

#include "serial.h"
#include <functional>

class HalfDuplexSerial;
using HalfDuplexSerial_ptr = std::shared_ptr<HalfDuplexSerial>;
using ConstHalfDuplexSerial_ptr = std::shared_ptr<const HalfDuplexSerial>;

class HalfDuplexSerial : public Serial {
public:
    HalfDuplexSerial(const std::string name,
                     const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num);

    void send_data(uint8_t slave_id, const std::string &data) const;
    std::string read_from_uart(uint32_t timeout = 20 / portTICK_RATE_MS) const;
    void check_for_commands(uint8_t slave_id, const std::function<void(const std::string &)> &handler) const;
};