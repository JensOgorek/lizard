#pragma once
#include <string>

namespace ota {

void init(const std::string ssid, const std::string password, const std::string url);
void shutdown();
void setup_wifi(const char *ssid, const char *password);
void attempt(const char *url);
void verify();

} // namespace ota