#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>

namespace runtime {
enum class Command { none, test, pair, pair_phone, pair_car, status, help, audio_phone_tone, audio_phone_loopback, audio_car_tone, audio_car_loopback, audio_off, invalid };

// A bounded, line-oriented USB console. Discard invalid/overlong lines in full
// so a suffix can never turn into an unintended command. CRLF runs only once.
class ConsoleCommands {
    char line_[32]{};
    size_t length_ = 0;
    bool discard_ = false;
public:
    Command feed(uint8_t byte) {
        if (byte != '\r' && byte != '\n') {
            if (byte < 32 || byte > 126 || length_ == sizeof(line_)) {
                discard_ = true;
            } else if (!discard_) {
                line_[length_++] = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
            }
            return Command::none;
        }
        std::string_view line(line_, length_);
        bool invalid = discard_;
        length_ = 0;
        discard_ = false;
        if (invalid) return Command::invalid;
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        while (!line.empty() && line.back() == ' ') line.remove_suffix(1);
        if (line.empty()) return Command::none;
        if (line.substr(0, 3) == "db ") return Command::none;
        if (line == "test") return Command::test;
        if (line == "pair") return Command::pair;
        if (line == "pair phone") return Command::pair_phone;
        if (line == "pair car") return Command::pair_car;
        if (line == "status") return Command::status;
        if (line == "audio phone tone") return Command::audio_phone_tone;
        if (line == "audio phone loopback") return Command::audio_phone_loopback;
        if (line == "audio car tone") return Command::audio_car_tone;
        if (line == "audio car loopback") return Command::audio_car_loopback;
        if (line == "audio off") return Command::audio_off;
        if (line == "help") return Command::help;
        return Command::invalid;
    }
};
// Separate from the human console so app identifiers retain their original case.
class SetupLines {
    char line_[160]{};
    size_t length_ = 0;
    bool discard_ = false;
public:
    std::string feed(uint8_t byte) {
        if (byte != '\r' && byte != '\n') {
            if (byte < 32 || byte > 126 || length_ == sizeof(line_)) discard_ = true;
            else if (!discard_) line_[length_++] = char(byte);
            return {};
        }
        std::string out;
        if (!discard_ && length_ >= 3 && line_[0] == 'd' && line_[1] == 'b' && line_[2] == ' ')
            out.assign(line_, length_);
        length_ = 0;
        discard_ = false;
        return out;
    }
};
} // namespace runtime
