/**
 * DRX Engine - Procedural sound synthesis
 */

#include "Audio/Public/SoundSynth.h"

#include <algorithm>
#include <cmath>

namespace DRX::Audio::Synth {

namespace {

constexpr float32 kTwoPi = 6.28318530717958647692f;

// Deterministic xorshift noise in [-1, 1]
struct Noise {
    uint32 state;
    explicit Noise(uint32 seed) : state(seed ? seed : 0x9E3779B9u) {}
    float32 Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float32>(state) / 2147483648.0f - 1.0f;
    }
};

float32 Oscillator(Waveform waveform, float32 phase, Noise& noise) {
    const float32 p = phase - std::floor(phase);
    switch (waveform) {
        case Waveform::Sine: return std::sin(p * kTwoPi);
        case Waveform::Square: return p < 0.5f ? 1.0f : -1.0f;
        case Waveform::Saw: return 2.0f * p - 1.0f;
        case Waveform::Triangle: return 4.0f * std::abs(p - 0.5f) - 1.0f;
        case Waveform::Noise: return noise.Next();
    }
    return 0.0f;
}

int16 ToPcm(float32 v) {
    return static_cast<int16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}

PcmData MakeBuffer(float32 duration) {
    PcmData pcm;
    pcm.channels = 1;
    pcm.sample_rate = kSampleRate;
    pcm.samples.resize(static_cast<size_t>(std::max(0.0f, duration) * kSampleRate));
    return pcm;
}

} // namespace

float32 Envelope::Evaluate(float32 t, float32 duration) const {
    if (t < 0.0f || t > duration) return 0.0f;
    float32 level = sustain;
    if (attack > 0.0f && t < attack) {
        level = t / attack;
    } else if (decay > 0.0f && t < attack + decay) {
        level = 1.0f + (sustain - 1.0f) * ((t - attack) / decay);
    }
    const float32 remaining = duration - t;
    if (release > 0.0f && remaining < release) {
        level *= remaining / release;
    }
    return level;
}

PcmData Tone(const ToneParams& params) {
    PcmData pcm = MakeBuffer(params.duration);
    Noise noise(params.seed);

    const float32 dt = 1.0f / static_cast<float32>(kSampleRate);
    const float32 alpha = params.lowpass > 0.0f ? 1.0f - std::exp(-kTwoPi * params.lowpass * dt) : 1.0f;
    float32 phase = 0.0f;
    float32 filtered = 0.0f;

    for (size_t i = 0; i < pcm.samples.size(); ++i) {
        const float32 t = static_cast<float32>(i) * dt;
        const float32 progress = params.duration > 0.0f ? t / params.duration : 0.0f;
        float32 frequency = params.start_frequency + (params.end_frequency - params.start_frequency) * progress;
        if (params.vibrato_rate > 0.0f) {
            frequency *= 1.0f + params.vibrato_depth * std::sin(kTwoPi * params.vibrato_rate * t);
        }
        phase += frequency * dt;

        const float32 raw = Oscillator(params.waveform, phase, noise);
        filtered += alpha * (raw - filtered);
        pcm.samples[i] = ToPcm(filtered * params.volume * params.envelope.Evaluate(t, params.duration));
    }
    return pcm;
}

PcmData Mix(const PcmData& a, const PcmData& b, float32 gain_b) {
    PcmData out;
    out.channels = 1;
    out.sample_rate = kSampleRate;
    out.samples.resize(std::max(a.samples.size(), b.samples.size()));
    for (size_t i = 0; i < out.samples.size(); ++i) {
        const float32 va = i < a.samples.size() ? a.samples[i] / 32767.0f : 0.0f;
        const float32 vb = i < b.samples.size() ? b.samples[i] / 32767.0f : 0.0f;
        out.samples[i] = ToPcm(va + vb * gain_b);
    }
    return out;
}

PcmData Gunshot(float32 volume, uint32 seed) {
    ToneParams crack;
    crack.waveform = Waveform::Noise;
    crack.duration = 0.35f;
    crack.volume = volume;
    crack.lowpass = 5000.0f;
    crack.seed = seed;
    crack.envelope = {0.001f, 0.06f, 0.25f, 0.28f};

    ToneParams thump;
    thump.waveform = Waveform::Sine;
    thump.start_frequency = 140.0f;
    thump.end_frequency = 45.0f;
    thump.duration = 0.2f;
    thump.volume = volume;
    thump.envelope = {0.001f, 0.05f, 0.4f, 0.14f};
    return Mix(Tone(crack), Tone(thump), 0.8f);
}

PcmData Hit(float32 volume) {
    ToneParams tick;
    tick.waveform = Waveform::Square;
    tick.start_frequency = 1400.0f;
    tick.end_frequency = 1100.0f;
    tick.duration = 0.07f;
    tick.volume = volume * 0.6f;
    tick.lowpass = 6000.0f;
    tick.envelope = {0.001f, 0.0f, 1.0f, 0.05f};
    return Tone(tick);
}

PcmData Headshot(float32 volume) {
    ToneParams ping;
    ping.waveform = Waveform::Triangle;
    ping.start_frequency = 2200.0f;
    ping.end_frequency = 2000.0f;
    ping.duration = 0.25f;
    ping.volume = volume;
    ping.envelope = {0.001f, 0.05f, 0.5f, 0.18f};
    return Mix(Hit(volume), Tone(ping), 0.7f);
}

PcmData Alarm(float32 duration, float32 low_hz, float32 high_hz, float32 cycles_per_second) {
    PcmData pcm = MakeBuffer(duration);
    const float32 dt = 1.0f / static_cast<float32>(kSampleRate);
    const Envelope envelope{0.02f, 0.0f, 1.0f, 0.05f};
    Noise unused(1);
    float32 phase = 0.0f;
    for (size_t i = 0; i < pcm.samples.size(); ++i) {
        const float32 t = static_cast<float32>(i) * dt;
        // Triangle sweep between the two pitches (classic two-tone siren)
        const float32 sweep = 1.0f - std::abs(2.0f * (t * cycles_per_second - std::floor(t * cycles_per_second)) - 1.0f);
        phase += (low_hz + (high_hz - low_hz) * sweep) * dt;
        const float32 v = 0.6f * Oscillator(Waveform::Saw, phase, unused) + 0.4f * Oscillator(Waveform::Square, phase, unused);
        pcm.samples[i] = ToPcm(v * 0.35f * envelope.Evaluate(t, duration));
    }
    return pcm;
}

PcmData Click(float32 volume) {
    ToneParams click;
    click.waveform = Waveform::Sine;
    click.start_frequency = 1800.0f;
    click.end_frequency = 1200.0f;
    click.duration = 0.03f;
    click.volume = volume;
    click.envelope = {0.001f, 0.0f, 1.0f, 0.025f};
    return Tone(click);
}

PcmData Footstep(float32 volume, uint32 seed) {
    ToneParams step;
    step.waveform = Waveform::Noise;
    step.duration = 0.12f;
    step.volume = volume;
    step.lowpass = 900.0f;
    step.seed = seed;
    step.envelope = {0.003f, 0.03f, 0.3f, 0.08f};
    return Tone(step);
}

} // namespace DRX::Audio::Synth
