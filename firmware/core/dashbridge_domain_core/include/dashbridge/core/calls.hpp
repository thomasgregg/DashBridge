#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dashbridge::core::calls {

constexpr size_t max_current_calls = 4;

struct CurrentCall {
    unsigned index = 0, direction = 0, status = 0, multiparty = 0;
    std::string number;
};

struct State {
    unsigned linked = 0, audio = 0, generation = 0;
    unsigned call = 0, setup = 0, held = 0, service = 0, signal = 0, roam = 0, battery = 0, incoming = 0;
    unsigned chld = 0, current_count = 0;
    std::string number;
    std::array<CurrentCall, max_current_calls> current{};
};

bool number_valid(const std::string &number);
bool decimal(const std::string &text, uint32_t &value);
unsigned chld_feature(const std::string &argument);
bool command_allowed(const State &state, const std::string &command, const std::string &argument);

class CommandGate {
    uint32_t boot_ = 0, last_ = 0;

  public:
    void reset(uint32_t peer);
    bool accept(uint32_t peer, uint32_t sequence);
};

enum class SnapshotResult : uint8_t { rejected, accepted, new_session };

class Controller {
    State local_, remote_;
    CommandGate commands_, test_commands_;
    uint32_t remote_boot_ = 0, remote_sequence_ = 0;

  public:
    State &local() { return local_; }
    const State &local() const { return local_; }
    State &remote() { return remote_; }
    const State &remote() const { return remote_; }
    uint32_t remote_boot() const { return remote_boot_; }
    CommandGate &commands() { return commands_; }
    CommandGate &test_commands() { return test_commands_; }

    SnapshotResult apply_remote_snapshot(uint32_t boot, uint32_t sequence, State state);
    void clear_remote_state();
};

} // namespace dashbridge::core::calls
