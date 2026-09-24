#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dashbridge::platform {

class EspRuntime {
  public:
    void initialize();
    int64_t now_ms() const;
    uint32_t random_token() const;
    bool classic_bond_known(const uint8_t *address) const;
    void lock();
    void unlock();
    int read_control(uint8_t *data, size_t size, uint32_t timeout_ms);
    bool write_control(const uint8_t *data, size_t size);
    int read_console(uint8_t *data, size_t size);
    void write_console(std::string_view text);
    bool button_pressed() const;
    void delay(uint32_t milliseconds) const;
    void erase_pairing_and_restart();
    bool has_ble_bond() const;
    bool has_classic_bond() const;
    const char *firmware_version() const;
    void log_memory() const;
    void info(const char *tag, const char *format, ...) const;
    void warn(const char *tag, const char *format, ...) const;

  private:
    void *mutex_ = nullptr;
};

} // namespace dashbridge::platform
