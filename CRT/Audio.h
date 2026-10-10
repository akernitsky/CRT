#pragma once

#include <xaudio2.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdint>

#pragma comment(lib, "xaudio2.lib")

// Samples stay alive until their voices have been destroyed.
class Audio
{
public:
    enum Effect { Channel, PowerOn, PowerOff, ClockTick };

    ~Audio()
    {
        for (auto* voice : effects_) if (voice) voice->DestroyVoice();
        if (noiseVoice_) noiseVoice_->DestroyVoice();
        if (toneVoice_) toneVoice_->DestroyVoice();
        if (distortedVoice_) distortedVoice_->DestroyVoice();
        if (musicVoice_) musicVoice_->DestroyVoice();
        if (master_) master_->DestroyVoice();
    }

    bool Init()
    {
        if (FAILED(XAudio2Create(&engine_)) || FAILED(engine_->CreateMasteringVoice(&master_)))
            return false;
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 1;
        format.nSamplesPerSec = rate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = sizeof(float);
        format.nAvgBytesPerSec = rate * sizeof(float);
        noise_.resize(rate * 2);
        for (auto& sample : noise_) sample = Random() * 0.12f;
        if (FAILED(engine_->CreateSourceVoice(&noiseVoice_, &format))) return false;
        XAUDIO2_BUFFER buffer = {};
        buffer.AudioBytes = static_cast<UINT32>(noise_.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(noise_.data());
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        if (FAILED(noiseVoice_->SubmitSourceBuffer(&buffer))) return false;
        // Exactly 1,000 cycles per second keeps the loop continuous.
        tone_.resize(rate);
        for (size_t i = 0; i < tone_.size(); ++i)
            tone_[i] = 0.08f * static_cast<float>(std::sin(6.283185307179586 * 1000.0 * static_cast<double>(i) / rate));
        if (FAILED(engine_->CreateSourceVoice(&toneVoice_, &format))) return false;
        toneVoice_->SetVolume(0.0f);
        buffer.AudioBytes = static_cast<UINT32>(tone_.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(tone_.data());
        if (FAILED(toneVoice_->SubmitSourceBuffer(&buffer))) return false;
        // An eight-second periodic carrier with mild wow, saturation and tape hiss.
        distorted_.resize(rate * 8);
        for (size_t i = 0; i < distorted_.size(); ++i)
        {
            const double t = static_cast<double>(i) / rate;
            const double phase = 6.283185307179586 * 1000.0 * t
                + 1.6 * std::sin(6.283185307179586 * 0.5 * t)
                + 0.12 * std::sin(6.283185307179586 * 7.0 * t);
            const double envelope = 0.88 + 0.08 * std::sin(6.283185307179586 * 0.25 * t)
                + 0.04 * std::sin(6.283185307179586 * 3.0 * t);
            const double edge = (std::min)(1.0, static_cast<double>((std::min)(i, distorted_.size() - 1 - i)) / 256.0);
            distorted_[i] = static_cast<float>(0.07 * envelope * std::tanh(1.15 * std::sin(phase))
                + 0.005 * edge * Random() + 0.002 * std::sin(6.283185307179586 * 50.0 * t));
        }
        if (FAILED(engine_->CreateSourceVoice(&distortedVoice_, &format))) return false;
        distortedVoice_->SetVolume(0.0f);
        buffer.AudioBytes = static_cast<UINT32>(distorted_.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(distorted_.data());
        if (FAILED(distortedVoice_->SubmitSourceBuffer(&buffer))) return false;
        MakeMusic();
        if (FAILED(engine_->CreateSourceVoice(&musicVoice_, &format))) return false;
        musicVoice_->SetVolume(0.0f);
        buffer.AudioBytes = static_cast<UINT32>(music_.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(music_.data());
        if (FAILED(musicVoice_->SubmitSourceBuffer(&buffer))) return false;
        for (int effect = 0; effect < 4; ++effect)
        {
            auto& samples = samples_[effect];
            const float duration = effect == ClockTick ? 0.045f : effect == PowerOff ? 0.9f : effect == PowerOn ? 0.22f : 0.12f;
            samples.resize(static_cast<size_t>(rate * duration));
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const float t = static_cast<float>(i) / rate;
                if (effect == ClockTick)
                {
                    // A short dry escapement click with a faint mechanical after-tap.
                    const float attack = (std::min)(1.0f,t * 10000.0f);
                    const float after = (std::max)(0.0f,t - 0.009f);
                    samples[i] = attack * 0.14f * std::exp(-t * 420.0f)
                        * (Random() * 0.7f + std::sin(6.2831853f * 2400.0f * t) * 0.3f)
                        + (t >= 0.009f ? 0.03f * std::exp(-after * 700.0f)
                            * std::sin(6.2831853f * 1700.0f * after) : 0.0f);
                    continue;
                }
                const float envelope = std::exp(-t * (effect == PowerOff ? 7.0f : 30.0f));
                const float click = Random() * std::exp(-t * 180.0f);
                const float tone = std::sin(6.2831853f * (effect == PowerOff ? 900.0f * t - 350.0f * t * t : 160.0f * t));
                samples[i] = 0.18f * envelope * (click + (effect == Channel ? Random() * 0.5f : tone * 0.5f));
            }
            if (FAILED(engine_->CreateSourceVoice(&effects_[effect], &format))) return false;
        }
        // Start only the audible loop in Update, after the UI applies its volume.
        ready_ = true;
        return true;
    }

    void Update(int channel, float power, int clockSeconds = -1)
    {
        if (!ready_) return;
        const float levels[] = {1.0f, 0.6f, 0.3f, 0.0f};
        const float audiblePower = muted_ ? 0.0f : power;
        UpdateLoop(noiseVoice_, 0, (channel < 3 ? levels[channel] : 0.0f) * audiblePower);
        UpdateLoop(toneVoice_, 1, channel >= 3 && channel < 10 && channel != 5 && channel != 8 && channel != 9 ? audiblePower * 0.5f : 0.0f);
        UpdateLoop(musicVoice_, 2, channel == 5 ? audiblePower : 0.0f);
        UpdateLoop(distortedVoice_, 3, channel == 9 ? audiblePower * 0.5f : 0.0f);
        const float tickVolume = channel == 8 ? audiblePower : 0.0f;
        if (tickVolume != tickVolume_)
        {
            effects_[ClockTick]->SetVolume(tickVolume);
            tickVolume_ = tickVolume;
        }
        if (clockSeconds >= 0)
        {
            if (lastClockSecond_ >= 0 && clockSeconds != lastClockSecond_
                && channel == 8 && power > 0.0f)
                Play(ClockTick);
            lastClockSecond_ = clockSeconds;
        }
    }

    void Play(Effect effect)
    {
        if (!ready_ || muted_) return;
        auto* voice = effects_[effect];
        voice->Stop();
        voice->FlushSourceBuffers();
        XAUDIO2_BUFFER buffer = {};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(samples_[effect].size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(samples_[effect].data());
        if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) voice->Start();
    }

    void SetVolume(float volume)
    {
        muted_ = volume <= 0.0f;
        if (master_) master_->SetVolume(volume);
    }
    bool Muted() const { return muted_; }

private:
    void UpdateLoop(IXAudio2SourceVoice* voice, size_t index, float volume)
    {
        if (volume == loopVolumes_[index]) return;
        if (volume <= 0)
            voice->Stop(); // Keep the queued buffer and resume at its paused position.
        else
        {
            voice->SetVolume(volume);
            if (loopVolumes_[index] <= 0) voice->Start();
        }
        loopVolumes_[index] = volume;
    }

    void MakeMusic()
    {
        // Original eight-bar lounge miniature, 96 BPM, C major with a turnaround.
        // Wrap note tails into the start so the musical loop has no silent seam.
        constexpr double beat = 60.0 / 96.0;
        constexpr double tau = 6.283185307179586;
        music_.assign(rate * 20,0.0f);
        auto note = [&](double start, int midi, double duration, float gain, bool bass)
        {
            const double frequency = 440.0 * std::pow(2.0,(midi - 69) / 12.0);
            const size_t first = static_cast<size_t>(start * rate);
            const size_t count = static_cast<size_t>((duration + 0.35) * rate);
            std::array<double, 6> weights{}, decays{}, frequencies{};
            const int harmonics = bass ? 4 : 6;
            for (int h = 0; h < harmonics; ++h)
            {
                const int harmonic = h + 1;
                weights[h] = bass ? 1.0 / (harmonic * harmonic) : 1.0 / std::pow(harmonic, 1.65);
                decays[h] = bass ? 4.5 : 2.2 + harmonic * 0.9;
                frequencies[h] = tau * frequency * harmonic * (1.0 + (bass ? 0 : harmonic * 0.00008));
            }
            for (size_t i = 0; i < count; ++i)
            {
                const double t = static_cast<double>(i) / rate;
                const double attack = 1.0 - std::exp(-t * (bass ? 170.0 : 500.0));
                const double release = std::exp(-(std::max)(0.0,t - duration) * 24.0);
                double sample = 0;
                for (int h = 0; h < harmonics; ++h)
                {
                    sample += weights[h] * std::exp(-t * decays[h]) * std::sin(frequencies[h] * t);
                }
                music_[(first + i) % music_.size()] += static_cast<float>(gain * attack * release * sample);
            }
        };
        const int chords[8][4] = {
            {60,64,67,71},{57,60,64,67},{62,65,69,72},{59,62,65,67},
            {59,62,64,67},{57,61,64,67},{62,65,69,72},{59,62,65,67}
        };
        const int roots[8] = {36,33,38,31,40,33,38,31};
        const int melody[32] = {
            76,79,74,71, 72,76,71,69, 69,77,76,74, 71,74,69,67,
            71,76,79,78, 76,73,72,69, 77,76,74,69, 71,74,67,72
        };
        for (int bar = 0; bar < 8; ++bar)
        {
            for (int pulse = 0; pulse < 4; ++pulse)
            {
                const double at = (bar * 4 + pulse) * beat;
                note(at,roots[bar] + (pulse % 2 ? 7 : 0),0.42,0.038f,true);
                note(at + (pulse % 2 ? beat * 0.08 : 0),melody[bar * 4 + pulse],0.38,0.042f,false);
                if (pulse == 0 || pulse == 2)
                    for (int chord = 0; chord < 4; ++chord)
                        note(at + beat * 0.66 + chord * 0.006,chords[bar][chord],0.30,0.014f,false);
                // Quiet brush swishes on the backbeat, with no percussion samples.
                const size_t first = static_cast<size_t>((at + beat * 0.66) * rate);
                float filtered = 0;
                for (size_t i = 0; i < rate / 10; ++i)
                {
                    const float t = static_cast<float>(i) / rate;
                    filtered += 0.18f * (Random() - filtered);
                    const float envelope = (1 - std::exp(-t * 450)) * std::exp(-t * 55);
                    music_[(first + i) % music_.size()] += filtered * envelope * (pulse % 2 ? 0.022f : 0.010f);
                }
            }
        }
        float peak = 0;
        for (float sample : music_) peak = (std::max)(peak,std::abs(sample));
        const float scale = peak > 0 ? 0.16f / peak : 1.0f;
        for (float& sample : music_) sample *= scale;
    }

    float Random()
    {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return static_cast<float>(seed_ & 0xffff) / 32767.5f - 1.0f;
    }
    static constexpr UINT32 rate = 48000;
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* noiseVoice_ = nullptr;
    IXAudio2SourceVoice* toneVoice_ = nullptr;
    IXAudio2SourceVoice* distortedVoice_ = nullptr;
    IXAudio2SourceVoice* musicVoice_ = nullptr;
    std::array<IXAudio2SourceVoice*, 4> effects_ = {};
    std::vector<float> noise_;
    std::vector<float> tone_;
    std::vector<float> distorted_;
    std::vector<float> music_;
    std::array<std::vector<float>, 4> samples_;
    int lastClockSecond_ = -1;
    uint32_t seed_ = 0x12345678;
    bool ready_ = false;
    bool muted_ = false;
    std::array<float, 4> loopVolumes_ = {};
    float tickVolume_ = -1;
};
