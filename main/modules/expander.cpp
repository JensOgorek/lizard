#include "expander.h"

#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>

Expander::Expander(const std::string name,
                   const ConstSerial_ptr serial,
                   const gpio_num_t boot_pin,
                   const gpio_num_t enable_pin,
                   void (*message_handler)(const char *))
    : Module(expander, name), serial(serial), boot_pin(boot_pin), enable_pin(enable_pin), message_handler(message_handler) {
    serial->enable_line_detection();
    gpio_reset_pin(boot_pin);
    gpio_reset_pin(enable_pin);
    gpio_set_direction(boot_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(boot_pin, 1);
    gpio_set_level(enable_pin, 0);
    delay(100);
    gpio_set_level(enable_pin, 1);
    char buffer[1024] = "";
    int len = 0;
    const unsigned long int start = millis();
    do {
        if (millis_since(start) > 1000) {
            throw std::runtime_error("expander is not booting");
        }
        if (serial->available()) {
            len = serial->read_line(buffer);
            strip(buffer, len);
            echo("%s: %s", name.c_str(), buffer);
        }
    } while (strncmp("Ready.", &buffer[1], 6));
}

void Expander::step() {
    static char buffer[1024];
    if (this->serial->available()) {
        int len = this->serial->read_line(buffer);
        check(buffer, len);
        if (buffer[0] == '!' && buffer[1] == '!') {
            this->message_handler(&buffer[2]);
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
    Module::step();
}

void Expander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[1024];
    int pos = std::sprintf(buffer, "core.%s(", method_name.c_str());
    pos += write_arguments_to_buffer(arguments, &buffer[pos]);
    pos += std::sprintf(&buffer[pos], ")\n");
    this->serial->write_chars(buffer, pos);
}
