#pragma once
#include "bridge_core.hpp"
#include "call_audio_test.hpp"
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
void call_audio_set(uint32_t local, uint32_t remote, unsigned sample_rate);
void call_audio_status();
std::string call_audio_diagnostics();
void call_audio_in(const uint8_t *data, uint32_t size);
uint32_t call_audio_out(uint8_t *data, uint32_t size);
}
