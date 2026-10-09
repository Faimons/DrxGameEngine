#pragma once

/**
 * DRX Engine - Procedural sound synthesis
 * Placeholder sounds (shots, hits, alarms, UI clicks) generated as PCM, so prototypes
 * have audio feedback before sound assets exist. All output is mono 16-bit.
 */

#include "Audio/Public/AudioSystem.h"

namespace DRX::Audio::Synth {

enum class Waveform { Sine, Square, Saw, Triangle, Noise };

struct Envelope {
    float32 attack = 0.005f;    // seconds
    float32 decay = 0.0f;       // seconds from peak to sustain level
    float32 sustain = 1.0f;     // level 0..1
    float32 release = 0.05f;    // seconds at the end

    float32 Evaluate(float32 t, float32 duration) const;
};

struct ToneParams {
    Waveform waveform = Waveform::Sine;
    float32 start_frequency = 440.0f;   // Hz
    float32 end_frequency = 440.0f;     // linear sweep over the duration
    float32 duration = 0.25f;
    float32 volume = 0.8f;
    Envelope envelope;
    float32 vibrato_rate = 0.0f;        // Hz
    float32 vibrato_depth = 0.0f;       // fraction of frequency
    float32 lowpass = 0.0f;             // one-pole cutoff in Hz, 0 = off
    uint32 seed = 1;                    // noise seed (deterministic output)
};

constexpr uint32 kSampleRate = 44100;

PcmData Tone(const ToneParams& params);

// Mixes b into a (lengths may differ); result is clipped to 16 bit
PcmData Mix(const PcmData& a, const PcmData& b, float32 gain_b = 1.0f);

// Ready-made placeholders
PcmData Gunshot(float32 volume = 0.9f, uint32 seed = 1);
PcmData Hit(float32 volume = 0.7f);
PcmData Headshot(float32 volume = 0.8f);
PcmData Alarm(float32 duration = 2.0f, float32 low_hz = 520.0f, float32 high_hz = 880.0f, float32 cycles_per_second = 1.5f);
PcmData Click(float32 volume = 0.5f);
PcmData Footstep(float32 volume = 0.4f, uint32 seed = 1);

} // namespace DRX::Audio::Synth
