#include "dashbridge/ports/runtime_services.hpp"
#include <cstdio>
#include <cstdlib>

namespace dashbridge::ports {
namespace {
RuntimeServices *installed = nullptr;
}

void install_runtime_services(RuntimeServices &services) { installed = &services; }

RuntimeServices &runtime_services() {
    if (!installed) std::abort();
    return *installed;
}

std::string setup_escape(const std::string &value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) {
            char code[7];
            std::snprintf(code, sizeof code, "\\u%04x", unsigned(c));
            out += code;
        } else out += char(c);
    }
    return out + '"';
}

} // namespace dashbridge::ports
