#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace contacts {
struct Number { std::string type, value; };
struct Contact {
    std::string name, timestamp;
    std::vector<Number> numbers;
    std::vector<std::string> addresses;
};

// Streaming, allocation-bounded vCard 2.1/3.0 parser. A complete vCard is
// emitted once, preserving its phone numbers, addresses and call timestamp.
class Parser {
    std::string line_, name_, fallback_, timestamp_;
    std::vector<Number> numbers_;
    std::vector<std::string> addresses_;
    bool card_ = false;
    uint32_t malformed_ = 0, overflow_ = 0;

    static std::string value(const std::string &line) {
        const auto colon = line.find(':');
        return colon == std::string::npos ? std::string{} : line.substr(colon + 1);
    }
    static std::string unescape(const std::string &input) {
        std::string out;
        for (size_t i = 0; i < input.size() && out.size() < 320; ++i) {
            if (input[i] == '\\' && i + 1 < input.size()) {
                const char next = input[++i];
                out += (next == 'n' || next == 'N') ? ' ' : next;
            } else if (uint8_t(input[i]) >= 32) out += input[i];
        }
        return out;
    }
    static std::string clean_number(const std::string &input) {
        std::string out;
        for (char c : input) {
            if ((c >= '0' && c <= '9') || c == '*' || c == '#') out += c;
            else if (c == '+' && out.empty()) out += c;
        }
        return out.size() <= 40 ? out : std::string{};
    }
    static std::string phone_type(const std::string &line) {
        const auto colon = line.find(':');
        std::string head = line.substr(0, colon);
        for (char &c : head) c = char(std::toupper(uint8_t(c)));
        for (const char *candidate : {"CELL", "HOME", "WORK", "FAX", "PAGER", "VOICE"})
            if (head.find(candidate) != std::string::npos) return candidate;
        return "VOICE";
    }
    static std::string clean_address(const std::string &raw) {
        const std::string input = unescape(raw);
        std::string out, part;
        auto append = [&] {
            while (!part.empty() && part.back() == ' ') part.pop_back();
            size_t first = 0; while (first < part.size() && part[first] == ' ') ++first;
            if (first < part.size()) {
                if (!out.empty()) out += ", ";
                out += part.substr(first);
            }
            part.clear();
        };
        for (char c : input) {
            if (c == ';') append();
            else if (uint8_t(c) >= 32) part += c;
        }
        append();
        return out.size() <= 320 ? out : out.substr(0, 320);
    }
    void reset_card() {
        name_.clear(); fallback_.clear(); timestamp_.clear();
        numbers_.clear(); addresses_.clear();
    }
    template<class Receive> void finish_line(Receive receive) {
        if (line_ == "BEGIN:VCARD") { card_ = true; reset_card(); }
        else if (line_ == "END:VCARD") {
            if (!card_) ++malformed_;
            else if (!numbers_.empty()) {
                Contact contact;
                contact.name = name_.empty() ? fallback_ : name_;
                if (contact.name.empty()) contact.name = numbers_[0].value;
                contact.timestamp = timestamp_;
                contact.numbers = numbers_;
                contact.addresses = addresses_;
                receive(contact);
            }
            card_ = false; reset_card();
        } else if (card_ && line_.rfind("FN", 0) == 0 && line_.find(':') != std::string::npos) {
            name_ = unescape(value(line_)).substr(0, 80);
        } else if (card_ && line_.rfind("N", 0) == 0 && line_.find(':') != std::string::npos) {
            auto raw = unescape(value(line_));
            const auto semi = raw.find(';');
            if (semi == std::string::npos) fallback_ = raw;
            else {
                const auto next = raw.find(';', semi + 1);
                const std::string given = raw.substr(semi + 1, next == std::string::npos ? next : next - semi - 1);
                fallback_ = given.empty() ? raw.substr(0, semi) : given + " " + raw.substr(0, semi);
            }
            while (!fallback_.empty() && fallback_.back() == ' ') fallback_.pop_back();
            if (fallback_.size() > 80) fallback_.resize(80);
        } else if (card_ && line_.rfind("TEL", 0) == 0 && line_.find(':') != std::string::npos) {
            auto number = clean_number(value(line_));
            if (!number.empty() && numbers_.size() < 6) numbers_.push_back({phone_type(line_), number});
            else if (!number.empty()) ++overflow_;
        } else if (card_ && line_.rfind("ADR", 0) == 0 && line_.find(':') != std::string::npos) {
            auto address = clean_address(value(line_));
            if (!address.empty() && addresses_.size() < 4) addresses_.push_back(std::move(address));
            else if (!address.empty()) ++overflow_;
        } else if (card_ && line_.rfind("X-IRMC-CALL-DATETIME", 0) == 0 && line_.find(':') != std::string::npos) {
            timestamp_ = value(line_).substr(0, 32);
        }
        line_.clear();
    }
  public:
    uint32_t malformed() const { return malformed_; }
    uint32_t overflow() const { return overflow_; }
    void reset() { line_.clear(); reset_card(); card_ = false; malformed_ = overflow_ = 0; }
    template<class Receive> void feed(const uint8_t *data, size_t size, Receive receive) {
        for (size_t i = 0; i < size; ++i) {
            const char c = char(data[i]);
            if (c == '\n') { finish_line(receive); continue; }
            if (c == '\r') continue;
            if (line_.size() >= 511) { ++overflow_; line_.clear(); card_ = false; continue; }
            if (uint8_t(c) >= 32 || c == '\t') line_ += c;
        }
    }
};
} // namespace contacts
