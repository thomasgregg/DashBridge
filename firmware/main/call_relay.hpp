#pragma once
#include "bridge_core.hpp"
#include "call_audio_test.hpp"
#include "music_protocol.hpp"
namespace runtime {
void relay_start();
void relay_poll();
void relay_receive(const bridge::WireMessage &m);
void relay_status();
void relay_audio_test(bool phone, calls::AudioTestMode mode);
void relay_audio_test_stop();
bool call_audio_test(calls::AudioTestMode mode);
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
void music_control_receive(const bridge::WireMessage &message);
void music_peer_connected(const uint8_t *address);
void music_peer_disconnected(const uint8_t *address);
bool music_audio_transport_send(const music::Audio &audio);
void music_audio_transport_receive(const music::Audio &audio);
#endif
#if CONFIG_BRIDGE_CONTACT_SYNC
void contacts_start();
void contacts_poll();
void contacts_status();
void contacts_receive(const bridge::WireMessage &m);
bridge::Phonebook &contacts_phonebook();
void contacts_peer_connected(const uint8_t *address);
void contacts_peer_disconnected(const uint8_t *address);
#endif
#if !CONFIG_BT_HFP_USE_EXTERNAL_CODEC
void call_audio_in(const uint8_t *data, uint32_t size);
uint32_t call_audio_out(uint8_t *data, uint32_t size);
#endif
}
