#include "dashbridge/core/calls.hpp"
#include "dashbridge/core/text.hpp"

#include <utility>

namespace dashbridge::core::calls {

bool number_valid(const std::string &number) {
    if (number.size() > 40)
        return false;
    for (const auto character : number)
        if ((character < '0' || character > '9') && character != '+' && character != '*' && character != '#')
            return false;
    return true;
}

bool decimal(const std::string &text, uint32_t &value) {
    return core::parse_uint32(text, value);
}

unsigned chld_feature(const std::string &argument) {
    if (argument == "0") return 0x01;
    if (argument == "1") return 0x02;
    if (argument.size() > 1 && argument[0] == '1') return 0x04;
    if (argument == "2") return 0x08;
    if (argument.size() > 1 && argument[0] == '2') return 0x10;
    if (argument == "3") return 0x20;
    if (argument == "4") return 0x40;
    return 0;
}

bool command_allowed(const State &state, const std::string &command, const std::string &argument) {
    if (!state.linked)
        return false;
    if (command == "chld") {
        const unsigned feature = chld_feature(argument);
        if (!feature || !(state.chld & feature))
            return false;
        if (feature == 0x04 || feature == 0x10) {
            uint32_t index = 0;
            if (!decimal(argument.substr(1), index) || !index)
                return false;
            bool found = false;
            for (size_t offset = 0; offset < state.current_count; ++offset)
                found |= state.current[offset].index == index;
            if (!found)
                return false;
        }
        if (argument == "0") return state.held || state.setup == 1;
        if (argument == "1" || argument == "2") return state.call && (state.held || state.setup == 1);
        return state.call && state.held;
    }
    if (state.held)
        return false;
    if (command == "dial") {
        if (state.call || state.setup || argument.empty() || !number_valid(argument))
            return false;
        bool digit = false;
        for (size_t offset = 0; offset < argument.size(); ++offset) {
            if (argument[offset] == '+' && offset != 0)
                return false;
            if (argument[offset] >= '0' && argument[offset] <= '9')
                digit = true;
        }
        return digit;
    }
    if (command == "redial") return !state.call && !state.setup && argument.empty();
    if (!state.generation) return false;
    if (command == "answer") return state.setup == 1 && !state.call && argument.empty();
    if (command == "hangup") return (state.call || state.setup) && argument.empty();
    if (command == "dtmf")
        return state.call && argument.size() == 1 &&
               ((argument[0] >= '0' && argument[0] <= '9') || argument[0] == '*' || argument[0] == '#' ||
                (argument[0] >= 'A' && argument[0] <= 'D'));
    return false;
}

void CommandGate::reset(uint32_t peer) {
    boot_ = peer;
    last_ = 0;
}

bool CommandGate::accept(uint32_t peer, uint32_t sequence) {
    if (!boot_ || boot_ != peer || !sequence || sequence <= last_)
        return false;
    last_ = sequence;
    return true;
}

SnapshotResult Controller::apply_remote_snapshot(uint32_t boot, uint32_t sequence, State state) {
    if (!boot || !sequence || (remote_boot_ == boot && sequence <= remote_sequence_))
        return SnapshotResult::rejected;
    const bool changed = remote_boot_ != boot;
    if (changed) {
        remote_boot_ = boot;
        commands_.reset(boot);
        test_commands_.reset(boot);
    }
    remote_sequence_ = sequence;
    remote_ = std::move(state);
    return changed ? SnapshotResult::new_session : SnapshotResult::accepted;
}

void Controller::clear_remote_state() {
    remote_ = {};
}

} // namespace dashbridge::core::calls
