#include "sdkconfig.h"

#if CONFIG_BRIDGE_CONTACT_SYNC
#include "dashbridge/core/contacts.hpp"
#include "dashbridge/protocols/contacts_v1.hpp"
#include "dashbridge/adapters/features.hpp"
#include "dashbridge/ports/runtime_services.hpp"

#include "esp_log.h"

#include <array>
#include <cstring>

#if CONFIG_BRIDGE_PHONE
extern "C" {
#include "esp_pbac_api.h"
}
#endif

namespace runtime {
using namespace dashbridge::ports;
namespace connections = dashbridge::core::connections;
namespace contact_core = dashbridge::core::contacts;
namespace contact_protocol = dashbridge::protocols::contacts_v1;
namespace dashlink = dashbridge::protocols::dashlink_v2;
namespace {

constexpr const char *tag = "contacts";
static contact_core::Store directory;

#if CONFIG_BRIDGE_PHONE
static bool profile_ready = false, peer_known = false, connecting = false, linked = false;
static bool pulling = false, downloaded = false, exported = false;
static esp_bd_addr_t peer = {};
static esp_pbac_conn_hdl_t handle = ESP_PBAC_INVALID_HANDLE;
static int64_t next_connect = 0, ack_deadline = 0;
static uint32_t bytes = 0, discarded = 0;
static size_t repository_index = 0;
static contact_protocol::Parser parser;
static contact_core::TransferSender transfer;

struct RepositoryPull {
    contact_core::Repository repository;
    const char *path;
    const char *name;
};

static constexpr std::array<RepositoryPull, 6> repositories = {{
    {contact_core::Repository::contacts, "/telecom/pb.vcf", "contacts"},
    {contact_core::Repository::favorites, "/telecom/fav.vcf", "favorites"},
    {contact_core::Repository::incoming, "/telecom/ich.vcf", "incoming calls"},
    {contact_core::Repository::outgoing, "/telecom/och.vcf", "outgoing calls"},
    {contact_core::Repository::missed, "/telecom/mch.vcf", "missed calls"},
    {contact_core::Repository::combined, "/telecom/cch.vcf", "combined calls"},
}};

static std::string encoded_phones(const contact_protocol::Contact &contact) {
    std::string result;
    for (const auto &phone : contact.numbers) {
        const std::string line = phone.type + "\t" + phone.value;
        if (result.size() + line.size() + (result.empty() ? 0 : 1) > 128)
            break;
        if (!result.empty())
            result += '\n';
        result += line;
    }
    return result;
}

static std::string encoded_addresses(const contact_protocol::Contact &contact) {
    std::string result;
    for (const auto &address : contact.addresses) {
        if (result.size() + address.size() + (result.empty() ? 0 : 1) > 640)
            break;
        if (!result.empty())
            result += '\n';
        result += address;
    }
    return result;
}

static void repository_finished(bool success) {
    if (!success)
        ++discarded;
    parser.reset();
    if (++repository_index < repositories.size())
        return;
    downloaded = true;
    transfer.source_ready();
    ESP_LOGI(tag,
             "Bluetooth directory downloaded: entries=%u contacts=%u favorites=%u incoming=%u "
             "outgoing=%u missed=%u bytes=%lu discarded=%lu",
             unsigned(directory.size()),
             unsigned(directory.size(contact_core::Repository::contacts)),
             unsigned(directory.size(contact_core::Repository::favorites)),
             unsigned(directory.size(contact_core::Repository::incoming)),
             unsigned(directory.size(contact_core::Repository::outgoing)),
             unsigned(directory.size(contact_core::Repository::missed)),
             (unsigned long)bytes, (unsigned long)discarded);
}

static void begin_transfer(bool complete) {
    transfer.start(random_token(), complete);
    exported = false;
    ack_deadline = 0;
}

static void callback(esp_pbac_event_t event, esp_pbac_param_t *parameter) {
    Guard guard;
    if (event == ESP_PBAC_INIT_EVT) {
        profile_ready = true;
        ESP_LOGI(tag, "iPhone phonebook and call-history service ready");
    } else if (event == ESP_PBAC_CONNECTION_STATE_EVT) {
        finish_classic_profile(connections::Profile::contacts);
        connecting = false;
        linked = parameter->conn_stat.connected;
        handle = linked ? parameter->conn_stat.handle : ESP_PBAC_INVALID_HANDLE;
        pulling = false;
        if (linked) {
            downloaded = false;
            directory.clear();
            parser.reset();
            bytes = discarded = 0;
            repository_index = 0;
            begin_transfer(false);
            ESP_LOGI(tag, "iPhone Bluetooth directory connected: repositories=0x%02x features=0x%08lx",
                     parameter->conn_stat.peer_supported_repo,
                     (unsigned long)parameter->conn_stat.peer_supported_feat);
        } else {
            downloaded = false;
            next_connect = now() + 10000;
            ESP_LOGW(tag, "iPhone Bluetooth directory disconnected: reason=%d",
                     parameter->conn_stat.reason);
        }
    } else if (event == ESP_PBAC_PULL_PHONE_BOOK_RESPONSE_EVT) {
        auto &response = parameter->pull_phone_book_rsp;
        if (!pulling || response.handle != handle)
            return;
        if (response.result != ESP_PBAC_SUCCESS) {
            pulling = false;
            ESP_LOGW(tag, "%s download unavailable: result=%d",
                     repositories[repository_index].name, response.result);
            repository_finished(false);
            return;
        }
        if (response.data && response.data_len) {
            bytes += response.data_len;
            parser.feed(response.data, response.data_len, [](const contact_protocol::Contact &contact) {
                contact_core::Entry entry;
                entry.repository = repositories[repository_index].repository;
                entry.name = contact.name;
                entry.phones = encoded_phones(contact);
                entry.addresses = encoded_addresses(contact);
                entry.timestamp = contact.timestamp;
                if (!directory.add(entry))
                    ++discarded;
            });
        }
        if (response.final) {
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
static contact_core::TransferReceiver transfer(directory);
#endif

} // namespace

contact_core::Store &contacts_phonebook() { return directory; }

void contacts_receive(const dashlink::Packet &packet) {
#if CONFIG_BRIDGE_PHONE
    const auto *message = std::get_if<dashlink::ContactAck>(&packet);
    if (!message) return;
    const contact_core::TransferAck ack{message->session, message->count, message->accepted};
    const auto result = transfer.accept(ack, directory.size());
    if (result == contact_core::AckResult::complete) {
        exported = true;
        ack_deadline = 0;
        ESP_LOGI(tag, "Tesla phonebook and call history ready: entries=%u",
                 unsigned(directory.size()));
    } else if (result == contact_core::AckResult::retry) {
        ack_deadline = 0;
        ESP_LOGW(tag, "Tesla rejected name lookup transfer; retrying");
    }
#else
    if (const auto *message = std::get_if<dashlink::ContactReset>(&packet)) {
        transfer.reset(message->session);
        ESP_LOGI(tag, "Receiving private phonebook and call-history transfer");
    } else if (const auto *message = std::get_if<dashlink::ContactEntry>(&packet)) {
        transfer.entry(message->session, message->sequence, message->entry);
    } else if (const auto *message = std::get_if<dashlink::ContactDone>(&packet)) {
        const auto result = transfer.done(message->session, message->count);
        if (result == contact_core::ReceiveResult::completed) {
            ESP_LOGI(tag, "Tesla phonebook ready: records=%u packed_bytes=%u",
                     unsigned(directory.size()), unsigned(directory.text_size()));
        } else if (result == contact_core::ReceiveResult::rejected) {
            ESP_LOGW(tag, "Incomplete name lookup transfer discarded");
        }
    }
#endif
}

void contacts_peer_connected(const uint8_t *address) {
#if CONFIG_BRIDGE_PHONE
    if (!address)
        return;
    memcpy(peer, address, sizeof peer);
    peer_known = true;
    next_connect = now() + 1000;
#else
    (void)address;
#endif
}

void contacts_peer_disconnected(const uint8_t *address) {
#if CONFIG_BRIDGE_PHONE
    if (address && peer_known && memcmp(peer, address, sizeof peer))
        return;
    peer_known = false;
    if (linked && handle != ESP_PBAC_INVALID_HANDLE)
        esp_pbac_disconnect(handle);
    linked = connecting = pulling = downloaded = false;
    finish_classic_profile(connections::Profile::contacts);
    handle = ESP_PBAC_INVALID_HANDLE;
    repository_index = 0;
    directory.clear();
    begin_transfer(true);
#else
    (void)address;
#endif
}

void contacts_poll() {
#if CONFIG_BRIDGE_PHONE
    if (profile_ready && peer_known && !linked && !connecting && now() >= next_connect &&
        try_begin_classic_profile(connections::Profile::contacts)) {
        const auto error = esp_pbac_connect(peer);
        connecting = error == ESP_OK;
        if (!connecting)
            finish_classic_profile(connections::Profile::contacts);
        next_connect = now() + 15000;
        ESP_LOGI(tag, "Phonebook connect request: %s", esp_err_to_name(error));
    }
    if (connecting && now() >= next_connect) {
        connecting = false;
        finish_classic_profile(connections::Profile::contacts);
        next_connect = now() + 10000;
    }
    if (linked && !pulling && !downloaded) {
        if (repository_index >= repositories.size()) {
            downloaded = true;
            transfer.source_ready();
            return;
        }
        parser.reset();
        esp_pbac_pull_phone_book_app_param_t options = {};
        options.include_property_selector = 1;
        options.property_selector = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) |
                                    (1ULL << 5) | (1ULL << 7) | (1ULL << 28);
        options.include_format = 1;
        options.format = 1;
        options.include_max_list_count = 1;
        options.max_list_count = contact_core::Store::max_entries;
        const auto error =
            esp_pbac_pull_phone_book(handle, repositories[repository_index].path, &options);
        pulling = error == ESP_OK;
        if (!pulling) {
            ESP_LOGW(tag, "%s pull request unavailable: %s",
                     repositories[repository_index].name, esp_err_to_name(error));
            repository_finished(false);
        }
    }
    switch (transfer.step(directory.size())) {
    case contact_core::SendStep::reset: {
        const dashlink::ContactReset reset{transfer.session()};
        if (send_to_car(reset))
            transfer.sent(directory.size());
        break;
    }
    case contact_core::SendStep::entry: {
        auto contact = directory.at(transfer.next());
        dashlink::ContactEntry entry{transfer.session(), uint32_t(transfer.next() + 1),
                                     std::move(contact)};
        if (send_to_car(entry))
            transfer.sent(directory.size());
        break;
    }
    case contact_core::SendStep::done: {
        const dashlink::ContactDone done{transfer.session(), uint32_t(directory.size())};
        if (send_to_car(done)) {
            transfer.sent(directory.size());
            ack_deadline = now() + 5000;
        }
        break;
    }
    case contact_core::SendStep::wait_ack:
        if (!exported && ack_deadline && now() >= ack_deadline) {
            ESP_LOGW(tag, "Tesla name lookup acknowledgement timed out; retrying");
            transfer.retry();
            ack_deadline = 0;
        }
        break;
    default:
        break;
    }
#else
    if (transfer.pending_ack()) {
        const auto ack_value = *transfer.pending_ack();
        const dashlink::ContactAck ack{ack_value.session, ack_value.count, ack_value.accepted};
        if (send_to_phone(ack))
            transfer.ack_sent();
    }
#endif
}

void contacts_status() {
#if CONFIG_BRIDGE_PHONE
    ESP_LOGI(tag,
             "Directory: linked=%d downloading=%d downloaded=%d Tesla_ready=%d entries=%u "
             "bytes=%lu discarded=%lu",
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
