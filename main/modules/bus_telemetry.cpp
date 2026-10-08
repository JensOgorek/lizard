#include "bus_telemetry.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <stdexcept>

static constexpr unsigned long MISSING_REPORT_MS = 5000; // frames came for this long without a declared name: say so once

static Module_ptr create_bus_telemetry(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, integer);
    const SerialBus_ptr bus = get_module_argument<SerialBus>(arguments[0]);
    const int64_t peer_id = arguments[1]->evaluate_integer();
    if (peer_id <= 0 || peer_id >= 255) {
        throw std::runtime_error("node ID must be between 1 and 254");
    }
    return std::make_shared<BusTelemetry>(name, bus, static_cast<uint8_t>(peer_id));
}
REGISTER_MODULE(BusTelemetry, &create_bus_telemetry)

const std::map<std::string, Variable_ptr> BusTelemetry::get_defaults() {
    return {
        {"age", std::make_shared<IntegerVariable>(0)},    // milliseconds since the last frame with one of our fields
        {"millis", std::make_shared<IntegerVariable>(0)}, // the peer's core.millis in that frame
        {"frames", std::make_shared<IntegerVariable>(0)},
    };
}

BusTelemetry::BusTelemetry(const std::string name, const SerialBus_ptr bus, const uint8_t peer_id)
    : Module(name), bus(bus), peer_id(peer_id) {
    this->properties = BusTelemetry::get_defaults();
    this->age = this->properties.at("age");
    this->peer_millis = this->properties.at("millis");
    this->frames = this->properties.at("frames");
    this->bus->add_telemetry_listener(this);
}

BusTelemetry::~BusTelemetry() {
    this->bus->remove_telemetry_listener(this);
}

void BusTelemetry::step() {
    this->age->set_integer_value(this->frame_seen ? millis_since(this->last_frame_millis) : millis());
    // the peer sends frames, but none of them carries a declared name: said once, the mirror keeps its start value
    if (!this->missing_reported && this->first_peer_frame_millis && millis_since(this->first_peer_frame_millis) > MISSING_REPORT_MS) {
        this->missing_reported = true;
        for (const std::string *const name : this->declared) {
            if (std::find(this->announced.begin(), this->announced.end(), name) == this->announced.end()) {
                echo("warning: %s: no telemetry frame of node %u carries \"%s\"", this->name.c_str(), this->peer_id, name->c_str());
            }
        }
    }
    Module::step();
}

void BusTelemetry::declare_property(const std::string &property_name, const Variable_ptr &variable) {
    if (property_name == "age" || property_name == "millis" || property_name == "frames") {
        throw std::runtime_error("\"" + property_name + "\" is a property of " + this->name + " itself");
    }
    if (variable->type != boolean && variable->type != integer && variable->type != number) {
        throw std::runtime_error("mirrored properties must be bool, int or float");
    }
    const BusTelemetry *const other = this->bus->declaring_listener(this->peer_id, property_name);
    if (other && other != this) {
        throw std::runtime_error("\"" + property_name + "\" is already declared by " + other->name);
    }
    if (this->declares(property_name)) {
        const Variable_ptr existing = this->properties.at(property_name);
        if (existing->type != variable->type) {
            throw std::runtime_error("\"" + property_name + "\" is already declared with another type");
        }
        return;
    }
    const auto inserted = this->properties.emplace(property_name, variable).first;
    this->declared.push_back(&inserted->first);
    this->missing_reported = false; // the new name gets its own report if no frame carries it
}

void BusTelemetry::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    throw std::runtime_error("\"" + this->name + "." + property_name + "\" mirrors node " + std::to_string(this->peer_id) +
                             " and is read-only");
}

bool BusTelemetry::declares(const std::string &name) const {
    return std::any_of(this->declared.begin(), this->declared.end(), [&](const std::string *key) { return *key == name; });
}

const std::string *BusTelemetry::declared_name(const Variable *variable) const {
    for (const std::string *name : this->declared) {
        if (this->properties.at(*name).get() == variable) {
            return name;
        }
    }
    return nullptr;
}

void BusTelemetry::reset_layout() {
    this->mapped.clear();
    this->slots.clear();
    this->announced.clear();
    this->first_peer_frame_millis = 0;
    this->missing_reported = false;
}

void BusTelemetry::forget_frame(const uint8_t frame_id) {
    for (Variable *const variable : this->mapped[frame_id]) {
        for (auto it = this->announced.begin(); variable && it != this->announced.end();) {
            it = this->properties.at(**it).get() == variable ? this->announced.erase(it) : std::next(it);
        }
    }
    this->mapped.erase(frame_id);
    this->slots.erase(frame_id);
}

void BusTelemetry::handle_layout_line(const telemetry::LayoutLine &line, const telemetry::Layout &layout) {
    const auto key = std::find_if(this->declared.begin(), this->declared.end(), [&](const std::string *name) { return *name == line.name; });
    if (key == this->declared.end()) {
        const auto frame = this->mapped.find(line.frame_id);
        if (frame != this->mapped.end() && line.index < frame->second.size() && frame->second[line.index]) {
            // another field where one of our mirrors was: the peer gave the frame id to other fields, so nothing of
            // our old mapping of it is safe
            this->forget_frame(line.frame_id);
            return;
        }
    }
    Variable *variable = nullptr;
    if (key != this->declared.end()) {
        if (std::find(this->announced.begin(), this->announced.end(), *key) == this->announced.end()) {
            this->announced.push_back(*key);
        }
        variable = this->properties.at(line.name).get();
        if (!telemetry::type_matches(*variable, line.type)) {
            // the mirror keeps its value, the rest of the frame is still mapped
            echo("warning: %s: node %u sends \"%s\" as type %c", this->name.c_str(), this->peer_id, line.name.c_str(), line.type);
            variable = nullptr;
        }
    } else if (!this->mapped.count(line.frame_id)) {
        return; // a frame without any of our mirrors
    }
    // every line of a frame we mirror sets its field anew: when the peer gave the frame id to other fields, the old
    // mirrors must not stay at their indices
    std::vector<Variable *> &variables = this->mapped[line.frame_id];
    if (line.count > 0 && variables.size() > line.count) {
        variables.resize(line.count);
    }
    if (variables.size() <= line.index) {
        variables.resize(line.index + 1, nullptr);
    }
    variables[line.index] = variable;
    if (std::none_of(variables.begin(), variables.end(), [](const Variable *v) { return v != nullptr; })) {
        this->mapped.erase(line.frame_id);
        this->slots.erase(line.frame_id);
        return;
    }
    const auto types = layout.frames.find(line.frame_id);
    if (types != layout.frames.end()) {
        this->slots[line.frame_id] = telemetry::map_frame(types->second, variables);
    }
}

bool BusTelemetry::handle_frame(const uint8_t frame_id, const uint8_t seq, const uint32_t peer_millis, const uint8_t *payload) {
    if (!this->first_peer_frame_millis) {
        this->first_peer_frame_millis = millis();
    }
    const auto it = this->slots.find(frame_id);
    if (it == this->slots.end() || it->second.empty()) {
        return false; // none of our fields, or its layout is not complete yet
    }
    telemetry::apply(it->second, payload);
    this->frame_seen = true;
    this->last_frame_millis = millis();
    this->peer_millis->set_integer_value(peer_millis);
    this->frames->set_integer_value(this->frames->integer_value() + 1);
    return true;
}
