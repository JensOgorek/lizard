#pragma once

#include "../utils/telemetry.h"
#include "module.h"
#include "serial_bus.h"
#include <map>
#include <string>
#include <vector>

// The core's view of one bus peer: declared properties that mirror fields of the frames the peer sends anyway. The
// coordinator passes every peer frame on to its host; this module copies its declared fields out of them on the way,
// without parsing, once the peer's layout lines have named them.
class BusTelemetry : public Module {
private:
    std::vector<const std::string *> declared;         // keys of the property map, in declaration order
    std::map<uint8_t, std::vector<Variable *>> mapped; // per frame: the mirror of each field index, nullptr if none
    std::map<uint8_t, std::vector<telemetry::Slot>> slots;
    std::vector<const std::string *> announced; // declared names that a layout line of the peer brought
    bool frame_seen = false;                    // a frame with one of our fields arrived
    unsigned long last_frame_millis = 0;
    unsigned long first_peer_frame_millis = 0; // when the first frame of the peer arrived, 0 before
    bool missing_reported = false;
    Variable_ptr age;
    Variable_ptr peer_millis;
    Variable_ptr frames;

    const std::string *declared_name(const Variable *variable) const;
    void forget_frame(uint8_t frame_id);

public:
    static inline constexpr const char *TYPE = "BusTelemetry";

    const SerialBus_ptr bus;
    const uint8_t peer_id;

    BusTelemetry(const std::string name, const SerialBus_ptr bus, uint8_t peer_id);
    ~BusTelemetry() override;
    void step() override;
    void declare_property(const std::string &property_name, const Variable_ptr &variable) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
    bool declares(const std::string &name) const;
    void reset_layout();
    void handle_layout_line(const telemetry::LayoutLine &line, const telemetry::Layout &layout);
    bool handle_frame(uint8_t frame_id, uint8_t seq, uint32_t peer_millis, const uint8_t *payload);
    static const std::map<std::string, Variable_ptr> get_defaults();
};
