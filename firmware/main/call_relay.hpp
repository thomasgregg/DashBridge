#pragma once
#include "bridge_core.hpp"
namespace runtime {
void relay_start();
void relay_poll();
void relay_receive(const bridge::WireMessage &m);
void relay_status();
bool relay_calls_ready();
void call_audio_start();
void call_audio_set(uint32_t local, uint32_t remote);
void call_audio_status();
void call_audio_in(const uint8_t *data, uint32_t size);
uint32_t call_audio_out(uint8_t *data, uint32_t size);
}
