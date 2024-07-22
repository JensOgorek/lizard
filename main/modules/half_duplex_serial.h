#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "module.h"
#include <functional>
#include <memory>
#include <string>

class HalfDuplexSerial;
using HalfDuplexSerial_ptr = std::shared_ptr<HalfDuplexSerial>;
using ConstHalfDuplexSerial_ptr = std::shared_ptr<const HalfDuplexSerial>;

class HalfDuplexSerial : public Module {
public:
    const gpio_num_t rx_pin;
    const gpio_num_t tx_pin;
    const long baud_rate;
    const uart_port_t uart_num;

    HalfDuplexSerial(const std::string name,
                     const gpio_num_t rx_pin, const gpio_num_t tx_pin, const long baud_rate, const uart_port_t uart_num);

    void enable_line_detection() const;
    void deinstall() const;
    int available() const;
    bool has_buffered_lines() const;
    int read(uint32_t timeout = 0) const;
    int read_line(char *buffer) const;
    size_t write(const uint8_t byte) const;
    void write_checked_line(const char *message, const int length) const;
    void flush() const;
    void clear() const;
    std::string get_output() const override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;

    void send_data_to_sub(uint8_t slave_id, const std::string &data) const;
    std::string read_from_uart(uint32_t timeout = 20 / portTICK_RATE_MS) const;
    void check_for_commands(uint8_t slave_id, const std::function<void(const std::string &)> &handler) const;
};