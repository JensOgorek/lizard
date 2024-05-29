#pragma once

#include "module.h"

class Wifi_test;
using Wifi_ptr = std::shared_ptr<Wifi_test>;

class Wifi_test : public Module {
private:
    void v1();
    void v2();
    void shutdown();
    void attempt_ota();

public:
    Wifi_test(const std::string ssid, const std::string password);
    // void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};