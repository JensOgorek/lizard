#include "external_expander.h"
#include "storage.h"
#include "utils/serial-replicator.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cstring>

ExternalExpander::ExternalExpander(const std::string name,
                                   const ConstSerial_ptr serial,
                                   const uint8_t address,
                                   MessageHandler message_handler)
    : Module(name), serial(serial), address(address), message_handler(message_handler) {
    serial->enable_line_detection();

    char buffer[1024] = "";
    int len = 0;
    const unsigned long int start = millis();
    do {
        if (millis_since(start) > 1000) {
            echo("warning: external expander is not booting");
            break;
        }
        if (serial->available()) {
            len = serial->read_line(buffer);
            strip(buffer, len);
            echo("%s: %s", name.c_str(), buffer);
        }
    } while (strcmp("Ready.", buffer));
}

void ExternalExpander::step() {
    static char buffer[1024];
    while (this->serial->has_buffered_lines()) {
        int len = this->serial->read_line(buffer);
        check(buffer, len);
        if (buffer[0] == '!' && buffer[1] == '!' && buffer[2] == address) {
            /* Don't trigger keep-alive from external expander updates */
            this->message_handler(&buffer[3], false, true);
        } else {
            echo("%s: %s", this->name.c_str(), buffer);
        }
    }
    Module::step();
}

void ExternalExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    static char buffer[1024];
    int pos = std::sprintf(buffer, "%02X:", address); // Add address prefix to the buffer

    if (method_name == "run") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        pos += std::sprintf(&buffer[pos], "%s", command.c_str());
        this->serial->write_checked_line(buffer, pos);
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        this->serial->deinstall();
    } else {
        pos += std::sprintf(&buffer[pos], "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos]);
        pos += std::sprintf(&buffer[pos], ")");
        this->serial->write_checked_line(buffer, pos);
    }
}
