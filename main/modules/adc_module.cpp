#include "adc_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"

Adc::Adc(const std::string name, uint8_t adc_unit, uint8_t channel, uint8_t attenuation_level)
    : Module(adc, name), adc_unit_(adc_unit), channel_(channel), attenuation_level_(attenuation_level) {
    if (!setup_adc(adc_unit_, channel_, attenuation_level_)) {
        throw std::runtime_error("ADC setup failed.");
    }

    this->properties["voltage"] = std::make_shared<NumberVariable>();
    this->properties["raw_value"] = std::make_shared<IntegerVariable>();
}

bool Adc::setup_adc(const int &adc_unit, const int &channel, const int &attenuation_level) {
    if (adc_unit == 1) {
        if (!channel1_mapper(channel, channel1_)) {
            echo("Failed: Invalid channel for ADC1.");
            return false;
        }
        if (!validate_attenuation_level(attenuation_level)) {
            echo("Failed: Invalid attenuation level for ADC1.");
            return false;
        }
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(channel1_, static_cast<adc_atten_t>(attenuation_level));
        esp_adc_cal_characterize(ADC_UNIT_1, static_cast<adc_atten_t>(attenuation_level), ADC_WIDTH_BIT_12, 1100, &adc_chars_);
        return true;
    } else if (adc_unit == 2) {
        if (!channel2_mapper(channel, channel2_)) {
            echo("Failed: Invalid channel for ADC2.");
            return false;
        }
        if (!validate_attenuation_level(attenuation_level)) {
            echo("Failed: Invalid attenuation level for ADC2.");
            return false;
        }
        adc2_config_channel_atten(channel2_, static_cast<adc_atten_t>(attenuation_level));
        esp_adc_cal_characterize(ADC_UNIT_2, static_cast<adc_atten_t>(attenuation_level), ADC_WIDTH_BIT_12, 1100, &adc_chars_);
        return true;
    }

    echo("Failed: Invalid or unsupported ADC module number.");
    return false;
}

bool Adc::channel1_mapper(const int &channel, adc1_channel_t &channel1) {
    if (channel >= 0 && channel < adc1_channel_t::ADC1_CHANNEL_MAX) {
        channel1 = static_cast<adc1_channel_t>(channel);
        return true;
    }
    return false;
}

bool Adc::channel2_mapper(const int &channel, adc2_channel_t &channel2) {
    if (channel >= 0 && channel < adc2_channel_t::ADC2_CHANNEL_MAX) {
        channel2 = static_cast<adc2_channel_t>(channel);
        return true;
    }
    return false;
}

bool Adc::validate_attenuation_level(const int &attenuation_level) {
    return attenuation_level == ADC_ATTEN_DB_0 ||
           attenuation_level == ADC_ATTEN_DB_2_5 ||
           attenuation_level == ADC_ATTEN_DB_6 ||
           attenuation_level == ADC_ATTEN_DB_11;
}

void Adc::step() {
    int32_t voltage = read_adc(adc_unit_, channel_, attenuation_level_);
    this->properties.at("voltage")->number_value = voltage;

    int32_t raw_value = read_adc_raw(adc_unit_, channel_, attenuation_level_);
    this->properties.at("raw_value")->integer_value = raw_value;

    Module::step();
}

int32_t Adc::read_adc(const int &adc_unit, const int &channel, const int &attenuation_level) {
    int32_t adc_reading = 0;
    int32_t voltage = 0;
    if (adc_unit == 1) {
        adc_reading = adc1_get_raw(channel1_);
        voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars_);
    } else if (adc_unit == 2) {
        adc2_get_raw(channel2_, ADC_WIDTH_BIT_12, &adc_reading);
        voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars_);
    }

    return voltage;
}

int32_t Adc::read_adc_raw(const int &adc_unit, const int &channel, const int &attenuation_level) {

    int32_t adc_reading = 0;
    if (adc_unit == 1) {
        adc_reading = adc1_get_raw(channel1_);
    } else if (adc_unit == 2) {
        adc2_get_raw(channel2_, ADC_WIDTH_BIT_12, &adc_reading);
    }

    return adc_reading;
}
