#pragma once

#include "half_duplex_serial.h"
#include "module.h"
#include <string>

class ExternalExpander;
using ExternalExpander_ptr = std::shared_ptr<ExternalExpander>;

class ExternalExpander : public Module {
public:
    const ConstHalfDuplexSerial_ptr half_duplex_serial;
    const uint8_t expander_id;
    MessageHandler message_handler;

    ExternalExpander(const std::string name,
                     const ConstHalfDuplexSerial_ptr half_duplex_serial,
                     uint8_t expander_id,
                     MessageHandler message_handler);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
