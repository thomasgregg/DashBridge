#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dashbridge::core::setup {

using Bytes = std::vector<uint8_t>;

enum class Preview : uint8_t { full = 0, sender = 1, app = 2 };

struct ApplicationRule {
    std::string id, name;
    Preview preview = Preview::full;
};

class ApplicationPolicy {
    std::vector<ApplicationRule> rules_;

  public:
    static constexpr size_t limit = 12;
    const std::vector<ApplicationRule> &rules() const { return rules_; }
    const ApplicationRule *find(const std::string &id) const;
    bool allow(const std::string &id, const std::string &name, Preview preview);
    bool deny(const std::string &id);
    Bytes serialize() const;
    bool load(const Bytes &data);
};

enum class Peer : uint8_t { phone, tesla };

struct Status {
    bool phone_bluetooth = false;
    bool notifications = false;
    bool phone_calls = false;
    bool internal_link = false;
    bool tesla_messages = false;
    bool tesla_transport = false;
    bool tesla_sync = false;
    bool tesla_calls = false;
    bool phone_pairing_open = false;
};

enum class CommandKind : uint8_t {
    allow_application,
    deny_application,
    open_phone_pairing,
    test_notification,
};

struct Command {
    CommandKind kind = CommandKind::allow_application;
    std::string application_id;
};

enum class CommandResult : uint8_t {
    applied,
    rejected,
};

class Port {
  public:
    virtual ~Port() = default;
    virtual Status status() = 0;
    virtual std::vector<std::string> allowed_application_ids() = 0;
    virtual CommandResult execute(const Command &command) = 0;
};

bool valid_application_id(std::string_view id);
std::string application_name_fallback(std::string_view id);

// All platform effects are behind this boundary. The controller owns setup
// policy, pairing windows and the status projection; adapters only provide
// time, persistence, a recent display name and the test-notification effect.
class Actions {
  public:
    virtual ~Actions() = default;
    virtual int64_t now_ms() const = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual std::string recent_application_name(std::string_view id) const = 0;
    virtual bool persist_policy(const Bytes &bytes) = 0;
    virtual bool start_test_notification() = 0;
};

class Controller final : public Port {
    Actions &actions_;
    ApplicationPolicy policy_;
    Status status_;
    int64_t pairing_until_[2]{};

  public:
    static constexpr int64_t pairing_window_ms = 120000;

    explicit Controller(Actions &actions) : actions_(actions) {}
    Status status() override;
    std::vector<std::string> allowed_application_ids() override;
    CommandResult execute(const Command &command) override;

    void update_status(Status status);
    bool restore_policy(const Bytes &bytes);
    std::vector<ApplicationRule> application_rules();
    bool find_application_rule(const std::string &id, ApplicationRule &rule);
    bool allow_application(const std::string &id, Preview preview);
    bool deny_application(const std::string &id);

    void open_pairing(Peer peer);
    bool pairing_allowed(Peer peer);
    void paired(Peer peer);
};

} // namespace dashbridge::core::setup
