#include "external_expander.h"
#include "utils/uart.h"

ExternalExpander::ExternalExpander(const std::string name,
                                   const ConstHalfDuplexSerial_ptr half_duplex_serial,
                                   uint8_t expander_id,
                                   MessageHandler message_handler)
    : Module(external_expander, name), half_duplex_serial(half_duplex_serial), expander_id(expander_id), message_handler(message_handler) {}

void ExternalExpander::step() {
    this->half_duplex_serial->check_for_commands(this->expander_id, [&](const std::string &message) {
        if (message[0] == '!' && message[1] == '!') {
            /* Don't trigger keep-alive from expander updates */
            this->message_handler(&message[2], false, true);
        } else {
            echo("%s: %s", this->name.c_str(), message.c_str());
        }
    });
    Module::step();
}

void ExternalExpander::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 1, string);
        std::string command = arguments[0]->evaluate_string();
        this->half_duplex_serial->send_data_to_sub(this->expander_id, command);
    } else if (method_name == "disconnect") {
        Module::expect(arguments, 0);
        this->half_duplex_serial->deinstall();
    } else {
        static char buffer[1024];
        int pos = std::sprintf(buffer, "core.%s(", method_name.c_str());
        pos += write_arguments_to_buffer(arguments, &buffer[pos]);
        pos += std::sprintf(&buffer[pos], ")");
        this->half_duplex_serial->send_data_to_sub(this->expander_id, std::string(buffer));
    }
}
