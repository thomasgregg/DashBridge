#pragma once

namespace calls {
enum class AudioTestMode { normal, tone, loopback };
}

namespace runtime {
void relay_audio_test(bool phone, calls::AudioTestMode mode);
void relay_audio_test_stop();
bool call_audio_test(calls::AudioTestMode mode);
}
