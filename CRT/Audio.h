#pragma once

#include <xaudio2.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>

#pragma comment(lib, "xaudio2.lib")

// Samples stay alive until their voices have been destroyed.
class Audio
{
public:
    enum Effect { Channel, PowerOn, PowerOff };

    ~Audio()
    {
        for (auto* voice : effects_) if (voice) voice->DestroyVoice();
        if (noiseVoice_) noiseVoice_->DestroyVoice();
        if (toneVoice_) toneVoice_->DestroyVoice();
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
        for (int effect = 0; effect < 3; ++effect)
        {
            auto& samples = samples_[effect];
            const float duration = effect == PowerOff ? 0.9f : effect == PowerOn ? 0.22f : 0.12f;
            samples.resize(static_cast<size_t>(rate * duration));
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const float t = static_cast<float>(i) / rate;
                const float envelope = std::exp(-t * (effect == PowerOff ? 7.0f : 30.0f));
                const float click = Random() * std::exp(-t * 180.0f);
                const float tone = std::sin(6.2831853f * (effect == PowerOff ? 900.0f * t - 350.0f * t * t : 160.0f * t));
                samples[i] = 0.18f * envelope * (click + (effect == Channel ? Random() * 0.5f : tone * 0.5f));
            }
            if (FAILED(engine_->CreateSourceVoice(&effects_[effect], &format))) return false;
        }
        if (FAILED(noiseVoice_->Start())) return false;
        if (FAILED(toneVoice_->Start()))
        {
            noiseVoice_->Stop();
            return false;
        }
        ready_ = true;
        return true;
    }

    void Update(int channel, float power)
    {
        if (!ready_) return;
        const float levels[] = {1.0f, 0.6f, 0.3f, 0.0f};
        noiseVoice_->SetVolume(levels[channel] * power);
        toneVoice_->SetVolume(channel == 3 ? power : 0.0f);
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

    void ToggleMute()
    {
        muted_ = !muted_;
        if (master_) master_->SetVolume(muted_ ? 0.0f : 1.0f);
    }
    bool Muted() const { return muted_; }

private:
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
    std::array<IXAudio2SourceVoice*, 3> effects_ = {};
    std::vector<float> noise_;
    std::vector<float> tone_;
    std::array<std::vector<float>, 3> samples_;
    uint32_t seed_ = 0x12345678;
    bool ready_ = false;
    bool muted_ = false;
};
