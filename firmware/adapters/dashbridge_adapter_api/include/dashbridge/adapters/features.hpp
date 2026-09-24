#pragma once

#include "dashbridge/adapters/call_control.hpp"
#include "dashbridge/core/contacts.hpp"
#include "dashbridge/core/setup.hpp"
#include "dashbridge/protocols/dashlink_v2.hpp"
#include "dashbridge/protocols/music_v1.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace runtime {

void phone_start();
void phone_poll();
void phone_receive(const dashbridge::protocols::dashlink_v2::Heartbeat &heartbeat);
bool phone_notifications_ready();
void phone_status();
bool phone_bluetooth_ready();
bool phone_board_link_ready();
bool phone_car_message_ready();
bool phone_car_transport_ready();
bool phone_car_sync_ready();
bool phone_car_call_ready();
void phone_setup_apps();
bool phone_setup_allow(const std::string &id, dashbridge::core::setup::Preview preview);
bool phone_setup_deny(const std::string &id);
bool phone_setup_discover();
bool phone_setup_test_notification();
std::string phone_setup_recent_application_name(std::string_view id);
bool phone_setup_save_policy(const dashbridge::core::setup::Bytes &bytes);

void car_start();
void car_poll();
void car_receive(const dashbridge::protocols::dashlink_v2::Heartbeat &heartbeat);
void car_receive(const dashbridge::protocols::dashlink_v2::Notification &notification);
bool car_test();
bool car_notifications_ready();
bool car_message_transport_ready();
bool car_message_sync_ready();
bool car_board_link_ready();
bool car_phone_notification_ready();
bool car_phone_bluetooth_ready();
bool car_phone_call_ready();

void relay_start();
void relay_poll();
void relay_receive(const dashbridge::protocols::dashlink_v2::Call &call);
void relay_status();
bool relay_calls_ready();
void call_audio_start();
void call_audio_register();
void call_audio_connection(uint16_t handle);
void call_audio_set(uint32_t local, uint32_t remote, unsigned sample_rate);
void call_audio_status();
std::string call_audio_diagnostics();

#if CONFIG_BRIDGE_MUSIC_RELAY
void music_start();
void music_poll();
void music_status();
void music_set_call_active(bool active);
bool music_audio_allowed();
void music_control_receive(const dashbridge::protocols::dashlink_v2::Packet &packet);
void music_peer_connected(const uint8_t *address);
void music_peer_disconnected(const uint8_t *address);
bool music_audio_transport_send(const music::Audio &audio);
void music_audio_transport_receive(const music::Audio &audio);
#endif

#if CONFIG_BRIDGE_CONTACT_SYNC
void contacts_start();
void contacts_poll();
void contacts_status();
void contacts_receive(const dashbridge::protocols::dashlink_v2::Packet &packet);
dashbridge::core::contacts::Store &contacts_phonebook();
void contacts_peer_connected(const uint8_t *address);
void contacts_peer_disconnected(const uint8_t *address);
#endif

#if !CONFIG_BT_HFP_USE_EXTERNAL_CODEC
void call_audio_in(const uint8_t *data, uint32_t size);
uint32_t call_audio_out(uint8_t *data, uint32_t size);
#endif

} // namespace runtime
