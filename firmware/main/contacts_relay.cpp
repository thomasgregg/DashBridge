#include "sdkconfig.h"
#if CONFIG_BRIDGE_CONTACT_SYNC
#include "call_relay.hpp"
#include "contacts_protocol.hpp"
#include "runtime.hpp"
#include "esp_log.h"
#include <array>
#include <cstring>
#if CONFIG_BRIDGE_PHONE
#include "esp_random.h"
extern "C" {
#include "esp_pbac_api.h"
}
#endif

namespace runtime {
namespace {
constexpr const char *tag = "contacts";
static bridge::Phonebook directory;
#if CONFIG_BRIDGE_PHONE
static bool profile_ready = false, peer_known = false, connecting = false, linked = false;
static bool pulling = false, downloaded = false, exported = false;
static esp_bd_addr_t peer = {};
static esp_pbac_conn_hdl_t handle = ESP_PBAC_INVALID_HANDLE;
static int64_t next_connect = 0, ack_deadline = 0;
static uint32_t bytes = 0, discarded = 0, transfer_session = 0;
static size_t transfer_index = 0;
static size_t repository_index = 0;
static bool reset_sent = false, source_complete = false, done_sent = false;
static contacts::Parser parser;
struct RepositoryPull { bridge::PhonebookRepository repository; const char *path; const char *name; };
static constexpr std::array<RepositoryPull, 6> repositories = {{
    {bridge::PhonebookRepository::contacts, "/telecom/pb.vcf", "contacts"},
    {bridge::PhonebookRepository::favorites, "/telecom/fav.vcf", "favorites"},
    {bridge::PhonebookRepository::incoming, "/telecom/ich.vcf", "incoming calls"},
    {bridge::PhonebookRepository::outgoing, "/telecom/och.vcf", "outgoing calls"},
    {bridge::PhonebookRepository::missed, "/telecom/mch.vcf", "missed calls"},
    {bridge::PhonebookRepository::combined, "/telecom/cch.vcf", "combined calls"},
}};

static std::string encoded_phones(const contacts::Contact &contact) {
    std::string result;
    for (const auto &phone : contact.numbers) {
        const std::string line = phone.type + "\t" + phone.value;
        if (result.size() + line.size() + (result.empty() ? 0 : 1) > 128) break;
        if (!result.empty()) result += '\n';
        result += line;
    }
    return result;
}
static std::string encoded_addresses(const contacts::Contact &contact) {
    std::string result;
    for (const auto &address : contact.addresses) {
        if (result.size() + address.size() + (result.empty() ? 0 : 1) > 640) break;
        if (!result.empty()) result += '\n';
        result += address;
    }
    return result;
}
static void repository_finished(bool success) {
    if (!success) ++discarded;
    parser.reset();
    if (++repository_index < repositories.size()) return;
    downloaded = source_complete = true;
    ESP_LOGI(tag, "Bluetooth directory downloaded: entries=%u contacts=%u favorites=%u incoming=%u outgoing=%u missed=%u bytes=%lu discarded=%lu",
             unsigned(directory.size()), unsigned(directory.size(bridge::PhonebookRepository::contacts)),
             unsigned(directory.size(bridge::PhonebookRepository::favorites)),
             unsigned(directory.size(bridge::PhonebookRepository::incoming)),
             unsigned(directory.size(bridge::PhonebookRepository::outgoing)),
             unsigned(directory.size(bridge::PhonebookRepository::missed)),
             (unsigned long)bytes, (unsigned long)discarded);
}

static uint32_t token() {
    uint32_t value;
    do { value = esp_random(); } while (!value);
    return value;
}
static void begin_transfer(bool complete) {
    transfer_session = token();
    transfer_index = 0;
    reset_sent = done_sent = exported = false;
    source_complete = complete;
    ack_deadline = 0;
}
static void retry_transfer() {
    transfer_index = 0;
    reset_sent = done_sent = exported = false;
    ack_deadline = 0;
}
static void callback(esp_pbac_event_t event, esp_pbac_param_t *param) {
    Guard guard;
    if (event == ESP_PBAC_INIT_EVT) {
        profile_ready = true;
        ESP_LOGI(tag, "iPhone phonebook and call-history service ready");
    } else if (event == ESP_PBAC_CONNECTION_STATE_EVT) {
        connecting = false;
        linked = param->conn_stat.connected;
        handle = linked ? param->conn_stat.handle : ESP_PBAC_INVALID_HANDLE;
        pulling = false;
        if (linked) {
            downloaded = false; directory.clear(); parser.reset(); bytes = discarded = 0; repository_index = 0;
            begin_transfer(false);
            ESP_LOGI(tag, "iPhone Bluetooth directory connected: repositories=0x%02x features=0x%08lx",
                     param->conn_stat.peer_supported_repo, (unsigned long)param->conn_stat.peer_supported_feat);
        } else {
            downloaded = false; next_connect = now() + 10000;
            ESP_LOGW(tag, "iPhone Bluetooth directory disconnected: reason=%d", param->conn_stat.reason);
        }
    } else if (event == ESP_PBAC_PULL_PHONE_BOOK_RESPONSE_EVT) {
        auto &r = param->pull_phone_book_rsp;
        if (!pulling || r.handle != handle) return;
        if (r.result != ESP_PBAC_SUCCESS) {
            pulling = false;
            ESP_LOGW(tag, "%s download unavailable: result=%d", repositories[repository_index].name, r.result);
            repository_finished(false);
            return;
        }
        if (r.data && r.data_len) {
            bytes += r.data_len;
            parser.feed(r.data, r.data_len, [](const contacts::Contact &contact) {
                bridge::PhonebookEntry entry;
                entry.repository = repositories[repository_index].repository;
                entry.name = contact.name;
                entry.phones = encoded_phones(contact);
                entry.addresses = encoded_addresses(contact);
                entry.timestamp = contact.timestamp;
                if (!directory.add(entry)) ++discarded;
            });
        }
        if (r.final) {
            pulling = false;
            ESP_LOGI(tag, "%s downloaded: entries=%u malformed=%lu overflow=%lu",
                     repositories[repository_index].name,
                     unsigned(directory.size(repositories[repository_index].repository)),
                     (unsigned long)parser.malformed(), (unsigned long)parser.overflow());
            repository_finished(true);
        }
    }
}
#else
static uint32_t active_session = 0, ack_session = 0;
static size_t expected_entries = 0;
static bool transfer_invalid = false;
#endif
} // namespace

bridge::Phonebook &contacts_phonebook() { return directory; }

void contacts_receive(const bridge::WireMessage &m) {
#if CONFIG_BRIDGE_PHONE
    if (m.op != bridge::Op::contact_ack || m.session != transfer_session) return;
    if (m.notice.body == "ok" && m.notice.id == directory.size()) {
        exported = true; ack_deadline = 0;
        ESP_LOGI(tag, "Tesla phonebook and call history ready: entries=%u", unsigned(directory.size()));
    } else {
        ESP_LOGW(tag, "Tesla rejected name lookup transfer; retrying");
        retry_transfer();
    }
#else
    if (m.op == bridge::Op::contact_reset) {
        directory.clear();
        active_session = m.session;
        expected_entries = 0;
        transfer_invalid = !active_session;
        ESP_LOGI(tag, "Receiving private phonebook and call-history transfer");
    } else if (m.op == bridge::Op::contact_entry) {
        uint32_t repository = 99;
        if (!m.notice.app.empty() && m.notice.app.size() == 1 && m.notice.app[0] >= '0' && m.notice.app[0] <= '5')
            repository = unsigned(m.notice.app[0] - '0');
        bridge::PhonebookEntry entry{bridge::PhonebookRepository(repository), m.notice.title,
                                     m.notice.subtitle, m.notice.body, m.notice.date};
        if (!active_session || m.session != active_session || m.notice.id != expected_entries + 1 ||
            repository > 5 || !directory.add(entry)) {
            transfer_invalid = true;
            return;
        }
        ++expected_entries;
    } else if (m.op == bridge::Op::contact_done) {
        if (!active_session || m.session != active_session || m.notice.id != expected_entries ||
            expected_entries != directory.size()) transfer_invalid = true;
        if (!transfer_invalid) {
            directory.ready(true);
            ESP_LOGI(tag, "Tesla phonebook ready: records=%u packed_bytes=%u",
                     unsigned(directory.size()), unsigned(directory.text_size()));
        } else {
            directory.clear();
            ESP_LOGW(tag, "Incomplete name lookup transfer discarded");
        }
        ack_session = active_session;
    }
#endif
}

void contacts_peer_connected(const uint8_t *address) {
#if CONFIG_BRIDGE_PHONE
    if (!address) return;
    memcpy(peer, address, sizeof peer); peer_known = true; next_connect = now() + 1000;
#else
    (void)address;
#endif
}
void contacts_peer_disconnected(const uint8_t *address) {
#if CONFIG_BRIDGE_PHONE
    if (address && peer_known && memcmp(peer, address, sizeof peer)) return;
    peer_known = false;
    if (linked && handle != ESP_PBAC_INVALID_HANDLE) esp_pbac_disconnect(handle);
    linked = connecting = pulling = downloaded = false; handle = ESP_PBAC_INVALID_HANDLE; repository_index = 0;
    directory.clear();
    begin_transfer(true);
#else
    (void)address;
#endif
}
void contacts_poll() {
#if CONFIG_BRIDGE_PHONE
    if (profile_ready && peer_known && !linked && !connecting && now() >= next_connect) {
        const auto error = esp_pbac_connect(peer);
        connecting = error == ESP_OK; next_connect = now() + 15000;
        ESP_LOGI(tag, "Phonebook connect request: %s", esp_err_to_name(error));
    }
    if (connecting && now() >= next_connect) { connecting = false; next_connect = now() + 10000; }
    if (linked && !pulling && !downloaded) {
        if (repository_index >= repositories.size()) { downloaded = source_complete = true; return; }
        parser.reset();
        esp_pbac_pull_phone_book_app_param_t options = {};
        options.include_property_selector = 1;
        options.property_selector = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) |
                                    (1ULL << 5) | (1ULL << 7) | (1ULL << 28);
        options.include_format = 1; options.format = 1;
        options.include_max_list_count = 1; options.max_list_count = bridge::Phonebook::max_entries;
        const auto error = esp_pbac_pull_phone_book(handle, repositories[repository_index].path, &options);
        pulling = error == ESP_OK;
        if (!pulling) {
            ESP_LOGW(tag, "%s pull request unavailable: %s", repositories[repository_index].name, esp_err_to_name(error));
            repository_finished(false);
        }
    }
    if (transfer_session && !reset_sent) {
        bridge::WireMessage reset{bridge::Op::contact_reset, transfer_session, {}};
        if (send_to_car(reset)) reset_sent = true;
    } else if (reset_sent && source_complete && transfer_index < directory.size()) {
        auto contact = directory.at(transfer_index);
        bridge::WireMessage entry{bridge::Op::contact_entry, transfer_session, {}};
        entry.notice.id = transfer_index + 1;
        entry.notice.app = std::to_string(unsigned(contact.repository));
        entry.notice.title = std::move(contact.name);
        entry.notice.subtitle = std::move(contact.phones);
        entry.notice.body = std::move(contact.addresses);
        entry.notice.date = std::move(contact.timestamp);
        if (send_to_car(entry)) ++transfer_index;
    } else if (reset_sent && source_complete && !done_sent) {
        bridge::WireMessage done{bridge::Op::contact_done, transfer_session, {}};
        done.notice.id = directory.size();
        if (send_to_car(done)) { done_sent = true; ack_deadline = now() + 5000; }
    } else if (done_sent && !exported && ack_deadline && now() >= ack_deadline) {
        ESP_LOGW(tag, "Tesla name lookup acknowledgement timed out; retrying");
        retry_transfer();
    }
#else
    if (ack_session) {
        bridge::WireMessage ack{bridge::Op::contact_ack, ack_session, {}};
        ack.notice.id = directory.size();
        ack.notice.body = directory.ready() ? "ok" : "retry";
        if (send_to_phone(ack)) ack_session = 0;
    }
#endif
}
void contacts_status() {
#if CONFIG_BRIDGE_PHONE
    ESP_LOGI(tag, "Directory: linked=%d downloading=%d downloaded=%d Tesla_ready=%d entries=%u bytes=%lu discarded=%lu",
             linked, pulling, downloaded, exported, unsigned(directory.size()),
             (unsigned long)bytes, (unsigned long)discarded);
#else
    ESP_LOGI(tag, "Directory: Tesla_phonebook_ready=%d entries=%u packed_bytes=%u",
             directory.ready(), unsigned(directory.size()), unsigned(directory.text_size()));
#endif
}
void contacts_start() {
#if CONFIG_BRIDGE_PHONE
    ESP_ERROR_CHECK(esp_pbac_register_callback(callback));
    ESP_ERROR_CHECK(esp_pbac_init());
#endif
}
} // namespace runtime
#endif
