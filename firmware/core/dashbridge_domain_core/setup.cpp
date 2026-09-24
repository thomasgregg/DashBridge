#include "dashbridge/core/setup.hpp"
#include "dashbridge/core/text.hpp"
#include <algorithm>

namespace dashbridge::core::setup {
namespace {

class Guard {
    Actions &actions_;

  public:
    explicit Guard(Actions &actions) : actions_(actions) { actions_.lock(); }
    ~Guard() { actions_.unlock(); }
};

size_t index(Peer peer) { return static_cast<size_t>(peer); }

} // namespace

bool valid_application_id(std::string_view id) {
    if (id.empty() || id.size() > 95) return false;
    for (unsigned char character : id) {
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '.' || character == '-' || character == '_';
        if (!valid) return false;
    }
    return true;
}

std::string application_name_fallback(std::string_view id) {
    const auto position = id.rfind('.');
    return core::clean_text(position == std::string_view::npos ? id : id.substr(position + 1), 48);
}

const ApplicationRule *ApplicationPolicy::find(const std::string &id) const {
    const auto rule = std::find_if(rules_.begin(), rules_.end(),
                                   [&](const ApplicationRule &candidate) {
                                       return candidate.id == id;
                                   });
    return rule == rules_.end() ? nullptr : &*rule;
}

bool ApplicationPolicy::allow(const std::string &id, const std::string &name, Preview preview) {
    if (!valid_application_id(id) || unsigned(preview) > 2) return false;
    auto cleaned = core::clean_text(name, 48);
    if (cleaned.empty()) cleaned = application_name_fallback(id);
    if (cleaned.empty()) return false;
    for (auto &rule : rules_) {
        if (rule.id == id) {
            rule.name = cleaned;
            rule.preview = preview;
            return true;
        }
    }
    if (rules_.size() >= limit) return false;
    rules_.push_back({id, cleaned, preview});
    return true;
}

bool ApplicationPolicy::deny(const std::string &id) {
    const auto rule = std::find_if(rules_.begin(), rules_.end(),
                                   [&](const ApplicationRule &candidate) {
                                       return candidate.id == id;
                                   });
    if (rule == rules_.end()) return false;
    rules_.erase(rule);
    return true;
}

Bytes ApplicationPolicy::serialize() const {
    Bytes result = {1, uint8_t(rules_.size())};
    for (const auto &rule : rules_) {
        result.push_back(uint8_t(rule.id.size()));
        result.insert(result.end(), rule.id.begin(), rule.id.end());
        result.push_back(uint8_t(rule.name.size()));
        result.insert(result.end(), rule.name.begin(), rule.name.end());
        result.push_back(uint8_t(rule.preview));
    }
    return result;
}

bool ApplicationPolicy::load(const Bytes &data) {
    if (data.size() < 2 || data[0] != 1 || data[1] > limit) return false;
    ApplicationPolicy parsed;
    size_t position = 2;
    for (unsigned i = 0; i < data[1]; ++i) {
        if (position >= data.size() || position + 1 + data[position] > data.size()) return false;
        size_t size = data[position++];
        std::string id(data.begin() + position, data.begin() + position + size);
        position += size;
        if (position >= data.size() || position + 1 + data[position] > data.size()) return false;
        size = data[position++];
        std::string name(data.begin() + position, data.begin() + position + size);
        position += size;
        if (position >= data.size() || parsed.find(id) ||
            !parsed.allow(id, name, Preview(data[position++])))
            return false;
    }
    if (position != data.size()) return false;
    rules_ = std::move(parsed.rules_);
    return true;
}

Status Controller::status() {
    Guard guard(actions_);
    auto result = status_;
    result.phone_pairing_open = actions_.now_ms() < pairing_until_[index(Peer::phone)];
    return result;
}

std::vector<std::string> Controller::allowed_application_ids() {
    Guard guard(actions_);
    std::vector<std::string> result;
    result.reserve(policy_.rules().size());
    for (const auto &rule : policy_.rules()) result.push_back(rule.id);
    return result;
}

CommandResult Controller::execute(const Command &command) {
    switch (command.kind) {
    case CommandKind::allow_application:
        return allow_application(command.application_id, Preview::full) ? CommandResult::applied
                                                                        : CommandResult::rejected;
    case CommandKind::deny_application:
        return deny_application(command.application_id) ? CommandResult::applied
                                                         : CommandResult::rejected;
    case CommandKind::open_phone_pairing:
        open_pairing(Peer::phone);
        return CommandResult::applied;
    case CommandKind::test_notification: {
        Guard guard(actions_);
        return actions_.start_test_notification() ? CommandResult::applied
                                                  : CommandResult::rejected;
    }
    }
    return CommandResult::rejected;
}

void Controller::update_status(Status status) {
    Guard guard(actions_);
    status.phone_pairing_open = false;
    status_ = status;
}

bool Controller::restore_policy(const Bytes &bytes) {
    Guard guard(actions_);
    ApplicationPolicy restored;
    if (!restored.load(bytes)) return false;
    policy_ = std::move(restored);
    return true;
}

std::vector<ApplicationRule> Controller::application_rules() {
    Guard guard(actions_);
    return policy_.rules();
}

bool Controller::find_application_rule(const std::string &id, ApplicationRule &rule) {
    Guard guard(actions_);
    const auto *found = policy_.find(id);
    if (!found) return false;
    rule = *found;
    return true;
}

bool Controller::allow_application(const std::string &id, Preview preview) {
    Guard guard(actions_);
    const auto prior = policy_;
    auto name = actions_.recent_application_name(id);
    if (name.empty()) {
        if (const auto *existing = policy_.find(id)) name = existing->name;
        else name = application_name_fallback(id);
    }
    if (!policy_.allow(id, name, preview) || !actions_.persist_policy(policy_.serialize())) {
        policy_ = prior;
        return false;
    }
    return true;
}

bool Controller::deny_application(const std::string &id) {
    Guard guard(actions_);
    const auto prior = policy_;
    if (!policy_.deny(id) || !actions_.persist_policy(policy_.serialize())) {
        policy_ = prior;
        return false;
    }
    return true;
}

void Controller::open_pairing(Peer peer) {
    Guard guard(actions_);
    pairing_until_[index(peer)] = actions_.now_ms() + pairing_window_ms;
}

bool Controller::pairing_allowed(Peer peer) {
    Guard guard(actions_);
    return actions_.now_ms() < pairing_until_[index(peer)];
}

void Controller::paired(Peer peer) {
    Guard guard(actions_);
    pairing_until_[index(peer)] = 0;
}

} // namespace dashbridge::core::setup
