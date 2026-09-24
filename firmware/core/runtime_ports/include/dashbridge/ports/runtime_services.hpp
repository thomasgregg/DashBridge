#pragma once

#include "dashbridge/core/setup.hpp"
#include "dashbridge/core/connections.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include <cstdint>
#include <string>

namespace dashbridge::ports {

enum class Peer { phone, car };

// Effects needed by Bluetooth adapters. The board app installs exactly one
// implementation at startup; adapters never depend on the app or ESP platform.
class RuntimeServices {
  public:
    virtual ~RuntimeServices() = default;
    virtual int64_t now_ms() const = 0;
    virtual uint32_t random_token() const = 0;
    virtual bool classic_bond_known(const uint8_t *address) const = 0;
    virtual void classic_base_link_changed(bool connected) = 0;
    virtual bool try_begin_classic_profile(core::connections::Profile profile) = 0;
    virtual void finish_classic_profile(core::connections::Profile profile) = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool send_to_phone(const protocols::dashlink_v2::Packet &packet) = 0;
    virtual bool send_to_car(const protocols::dashlink_v2::Packet &packet) = 0;
    virtual bool pairing_allowed(Peer peer) const = 0;
    virtual void paired(Peer peer) = 0;
    virtual core::setup::Controller &setup_controller() = 0;
    virtual void emit_setup_json(const std::string &json) = 0;
};

void install_runtime_services(RuntimeServices &services);
RuntimeServices &runtime_services();

class Guard {
  public:
    Guard() { runtime_services().lock(); }
    ~Guard() { runtime_services().unlock(); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

inline int64_t now() { return runtime_services().now_ms(); }
inline uint32_t random_token() { return runtime_services().random_token(); }
inline bool classic_bond_known(const uint8_t *address) {
    return runtime_services().classic_bond_known(address);
}
inline void classic_base_link_changed(bool connected) {
    runtime_services().classic_base_link_changed(connected);
}
inline bool try_begin_classic_profile(core::connections::Profile profile) {
    return runtime_services().try_begin_classic_profile(profile);
}
inline void finish_classic_profile(core::connections::Profile profile) {
    runtime_services().finish_classic_profile(profile);
}
inline bool send_to_phone(const protocols::dashlink_v2::Packet &packet) {
    return runtime_services().send_to_phone(packet);
}
inline bool send_to_car(const protocols::dashlink_v2::Packet &packet) {
    return runtime_services().send_to_car(packet);
}
inline bool pairing_allowed(Peer peer) { return runtime_services().pairing_allowed(peer); }
inline void paired(Peer peer) { runtime_services().paired(peer); }
inline core::setup::Controller &setup_controller() { return runtime_services().setup_controller(); }
inline void setup_emit(const std::string &json) { runtime_services().emit_setup_json(json); }

std::string setup_escape(const std::string &value);

} // namespace dashbridge::ports
