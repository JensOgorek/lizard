#include "bluetooth.h"
#include "core.h"
#include "uart.h"

// somewhat of a hack, maybe call ->keep_alive in parse_lizard?
extern Core_ptr core_module;

Bluetooth::Bluetooth(const std::string name, const std::string device_name, void (*message_handler)(const char *))
    : Module(bluetooth, name), device_name(device_name) {
    ZZ::BleCommand::init(device_name, [message_handler](const std::string_view &message) {
        try {
            std::string message_string(message.data(), message.length());
            message_handler(message_string.c_str());
            core_module->keep_alive();
        } catch (const std::exception &e) {
            echo("error in bluetooth message handler: %s", e.what());
        }
    });
}

void Bluetooth::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        expect(arguments, 1, string);
        ZZ::BleCommand::send(arguments[0]->evaluate_string());
    } else {
        Module::call(method_name, arguments);
    }
}
