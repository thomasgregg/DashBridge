#include "dashbridge/core/contacts.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace dashbridge::core::contacts {

void Store::clear() {
    entries_.clear();
    text_.clear();
    ready_ = false;
}

static bool phone_lines_valid(const std::string &phones) {
    if (phones.empty() || phones.size() > 128 || phones.front() == '\n' || phones.back() == '\n')
        return false;
    size_t at = 0;
    while (at < phones.size()) {
        const size_t end = phones.find('\n', at);
        const size_t stop = end == std::string::npos ? phones.size() : end;
        const size_t tab = phones.find('\t', at);
        if (tab == std::string::npos || tab >= stop || tab == at || tab + 1 == stop)
            return false;
        for (size_t i = at; i < tab; ++i)
            if (!(std::isalnum(uint8_t(phones[i])) || phones[i] == '-'))
                return false;
        for (size_t i = tab + 1; i < stop; ++i)
            if (!std::isdigit(uint8_t(phones[i])) && phones[i] != '+' && phones[i] != '*' &&
                phones[i] != '#')
                return false;
        at = stop + 1;
    }
    return true;
}

bool Store::add(const Entry &entry) {
    const unsigned repository = unsigned(entry.repository);
    if (repository > unsigned(Repository::combined) || entry.name.empty() || entry.name.size() > 80 ||
        !phone_lines_valid(entry.phones) || entry.addresses.size() > 640 ||
        entry.timestamp.size() > 32 || entries_.size() >= max_entries ||
        text_.size() + entry.name.size() + entry.phones.size() + entry.addresses.size() +
                entry.timestamp.size() >
            max_text)
        return false;
    for (char character : entry.addresses)
        if (uint8_t(character) < 32 && character != '\n')
            return false;
    for (char character : entry.timestamp)
        if (uint8_t(character) < 32)
            return false;
    for (size_t index = 0; index < entries_.size(); ++index) {
        const auto existing = at(index);
        if (existing.repository == entry.repository && existing.name == entry.name &&
            existing.phones == entry.phones && existing.addresses == entry.addresses &&
            existing.timestamp == entry.timestamp)
            return true;
    }
    Ref reference{};
    reference.repository = uint8_t(entry.repository);
    reference.name_at = uint16_t(text_.size());
    reference.name_size = uint8_t(entry.name.size());
    text_.insert(text_.end(), entry.name.begin(), entry.name.end());
    reference.phones_at = uint16_t(text_.size());
    reference.phones_size = uint8_t(entry.phones.size());
    text_.insert(text_.end(), entry.phones.begin(), entry.phones.end());
    reference.addresses_at = uint16_t(text_.size());
    reference.addresses_size = uint16_t(entry.addresses.size());
    text_.insert(text_.end(), entry.addresses.begin(), entry.addresses.end());
    reference.timestamp_at = uint16_t(text_.size());
    reference.timestamp_size = uint8_t(entry.timestamp.size());
    text_.insert(text_.end(), entry.timestamp.begin(), entry.timestamp.end());
    entries_.push_back(reference);
    return true;
}

bool Store::add(const std::string &name, const std::string &number) {
    return add({Repository::contacts, name, "VOICE\t" + number, {}, {}});
}

size_t Store::size(Repository repository) const {
    return std::count_if(entries_.begin(), entries_.end(), [&](const Ref &reference) {
        return reference.repository == uint8_t(repository);
    });
}

Entry Store::at(size_t index) const {
    if (index >= entries_.size())
        return {};
    const auto &reference = entries_[index];
    return {Repository(reference.repository),
            std::string(text_.data() + reference.name_at, reference.name_size),
            std::string(text_.data() + reference.phones_at, reference.phones_size),
            std::string(text_.data() + reference.addresses_at, reference.addresses_size),
            std::string(text_.data() + reference.timestamp_at, reference.timestamp_size)};
}

ReceiveResult TransferReceiver::reset(uint32_t session) {
    if (!session)
        return ReceiveResult::rejected;
    staged_.clear();
    expected_sequence_ = 0;
    pending_ack_.reset();
    active_session_ = session;
    invalid_ = false;
    return ReceiveResult::accepted;
}

ReceiveResult TransferReceiver::entry(uint32_t session, size_t sequence, const Entry &entry_value) {
    if (!active_session_ || session != active_session_)
        return ReceiveResult::ignored;
    if (invalid_ || sequence != expected_sequence_ + 1 || !staged_.add(entry_value)) {
        invalid_ = true;
        return ReceiveResult::rejected;
    }
    ++expected_sequence_;
    return ReceiveResult::accepted;
}

ReceiveResult TransferReceiver::done(uint32_t session, size_t count) {
    if (!active_session_ || session != active_session_)
        return ReceiveResult::ignored;
    // The wire count tracks ordered source frames. Exact duplicate records are
    // intentionally coalesced by Store, so committed size may be smaller.
    const bool accepted = !invalid_ && count == expected_sequence_;
    if (accepted) {
        staged_.ready(true);
        store_ = std::move(staged_);
        staged_ = Store{};
    } else {
        staged_.clear();
    }
    pending_ack_ = TransferAck{session, count, accepted};
    active_session_ = 0;
    return accepted ? ReceiveResult::completed : ReceiveResult::rejected;
}

bool TransferSender::start(uint32_t session, bool ready) {
    if (!session)
        return false;
    session_ = session;
    next_ = 0;
    source_ready_ = ready;
    step_ = SendStep::reset;
    return true;
}

void TransferSender::source_ready() {
    source_ready_ = true;
    if (step_ == SendStep::wait_source)
        step_ = SendStep::entry;
}

void TransferSender::retry() {
    if (!session_)
        return;
    next_ = 0;
    step_ = SendStep::reset;
}

SendStep TransferSender::step(size_t total) const {
    if (step_ == SendStep::entry && next_ >= total)
        return SendStep::done;
    return step_;
}

void TransferSender::sent(size_t total) {
    switch (step(total)) {
    case SendStep::reset:
        step_ = source_ready_ ? SendStep::entry : SendStep::wait_source;
        break;
    case SendStep::entry:
        ++next_;
        step_ = next_ >= total ? SendStep::done : SendStep::entry;
        break;
    case SendStep::done:
        step_ = SendStep::wait_ack;
        break;
    default:
        break;
    }
}

AckResult TransferSender::accept(const TransferAck &ack, size_t total) {
    if (step_ != SendStep::wait_ack || ack.session != session_)
        return AckResult::ignored;
    if (ack.accepted && ack.count == total) {
        step_ = SendStep::complete;
        return AckResult::complete;
    }
    retry();
    return AckResult::retry;
}

} // namespace dashbridge::core::contacts
