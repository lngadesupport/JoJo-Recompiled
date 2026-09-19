#ifdef _WIN32
#define NOMINMAX
#include "audio_host.h"

#include <limits>
#include <memory>
#include <utility>

namespace jojo {

float xaudio2_gain_from_percent(int percent) noexcept {
    if (percent <= 0) return 0.0f;
    if (percent >= 100) return 1.0f;
    return static_cast<float>(percent) / 100.0f;
}

Result<XAudio2PcmPlan> make_xaudio2_pcm_plan(
    std::span<const std::int16_t> interleaved_stereo) noexcept {
    if (interleaved_stereo.empty()) {
        return Result<XAudio2PcmPlan>::failure(
            ErrorCode::invalid_argument,
            "XAudio2 PCM submission is empty");
    }
    if ((interleaved_stereo.size() % 2u) != 0u) {
        return Result<XAudio2PcmPlan>::failure(
            ErrorCode::invalid_argument,
            "XAudio2 PCM submission must contain interleaved stereo samples");
    }
    if (interleaved_stereo.size() >
        std::numeric_limits<std::uint32_t>::max() / sizeof(std::int16_t)) {
        return Result<XAudio2PcmPlan>::failure(
            ErrorCode::invalid_argument,
            "XAudio2 PCM submission is too large");
    }

    XAudio2PcmPlan plan{};
    plan.frame_count = interleaved_stereo.size() / 2u;
    plan.byte_size = interleaved_stereo.size() * sizeof(std::int16_t);
    plan.samples = interleaved_stereo.data();
    return Result<XAudio2PcmPlan>::success(plan);
}

XAudio2Ps1AudioHost::OwnedBuffer*
XAudio2Ps1AudioHost::VoiceCallback::acquire() {
    std::lock_guard lock(mutex_);
    if (!free_buffers_.empty()) {
        auto* buffer = free_buffers_.back();
        free_buffers_.pop_back();
        return buffer;
    }
    return new OwnedBuffer{};
}

void XAudio2Ps1AudioHost::VoiceCallback::track(OwnedBuffer* buffer) {
    std::lock_guard lock(mutex_);
    outstanding_.insert(buffer);
}

void XAudio2Ps1AudioHost::VoiceCallback::discard(OwnedBuffer* buffer) noexcept {
    try {
        std::lock_guard lock(mutex_);
        const auto it = outstanding_.find(buffer);
        if (it == outstanding_.end()) return;
        auto* owned = *it;
        outstanding_.erase(it);
        owned->samples.clear();
        free_buffers_.push_back(owned);
    } catch (...) {
        return;
    }
}

void XAudio2Ps1AudioHost::VoiceCallback::release_all() noexcept {
    std::vector<OwnedBuffer*> buffers;
    try {
        std::lock_guard lock(mutex_);
        buffers.reserve(outstanding_.size() + free_buffers_.size());
        for (auto* buffer : outstanding_) buffers.push_back(buffer);
        for (auto* buffer : free_buffers_) buffers.push_back(buffer);
        outstanding_.clear();
        free_buffers_.clear();
    } catch (...) {
        return;
    }
    for (auto* buffer : buffers) delete buffer;
}

void STDMETHODCALLTYPE XAudio2Ps1AudioHost::VoiceCallback::OnBufferEnd(
    void* context) {
    discard(static_cast<OwnedBuffer*>(context));
}

XAudio2Ps1AudioHost::~XAudio2Ps1AudioHost() {
    if (source_voice_) {
        source_voice_->Stop(0u);
        source_voice_->FlushSourceBuffers();
        source_voice_->DestroyVoice();
        source_voice_ = nullptr;
    }
    if (mastering_voice_) {
        mastering_voice_->DestroyVoice();
        mastering_voice_ = nullptr;
    }
    callback_.release_all();
    engine_.Reset();
}

Result<std::unique_ptr<XAudio2Ps1AudioHost>> XAudio2Ps1AudioHost::create() {
    auto host = std::unique_ptr<XAudio2Ps1AudioHost>(
        new XAudio2Ps1AudioHost());

    auto hr = XAudio2Create(
        host->engine_.GetAddressOf(),
        0u,
        XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        return Result<std::unique_ptr<XAudio2Ps1AudioHost>>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2Create failed");
    }

    hr = host->engine_->CreateMasteringVoice(&host->mastering_voice_);
    if (FAILED(hr)) {
        return Result<std::unique_ptr<XAudio2Ps1AudioHost>>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 mastering voice creation failed");
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2u;
    format.nSamplesPerSec = 44100u;
    format.wBitsPerSample = 16u;
    format.nBlockAlign = static_cast<WORD>(
        format.nChannels * format.wBitsPerSample / 8u);
    format.nAvgBytesPerSec =
        format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0u;

    hr = host->engine_->CreateSourceVoice(
        &host->source_voice_,
        &format,
        0u,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        &host->callback_);
    if (FAILED(hr)) {
        return Result<std::unique_ptr<XAudio2Ps1AudioHost>>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 source voice creation failed");
    }

    hr = host->source_voice_->Start(0u);
    if (FAILED(hr)) {
        return Result<std::unique_ptr<XAudio2Ps1AudioHost>>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 source voice start failed");
    }

    return Result<std::unique_ptr<XAudio2Ps1AudioHost>>::success(
        std::move(host));
}

Result<void> XAudio2Ps1AudioHost::submit(
    std::span<const std::int16_t> interleaved_stereo) {
    if (!source_voice_) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 source voice is unavailable");
    }

    const auto plan = make_xaudio2_pcm_plan(interleaved_stereo);
    if (!plan) {
        return Result<void>::failure(plan.error, plan.detail);
    }

    std::unique_ptr<OwnedBuffer> owned(callback_.acquire());
    owned->samples.assign(
        interleaved_stereo.begin(),
        interleaved_stereo.end());

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(
        owned->samples.size() * sizeof(std::int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(
        owned->samples.data());
    buffer.pContext = owned.get();

    callback_.track(owned.get());
    auto* context = owned.release();

    const auto hr = source_voice_->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        callback_.discard(context);
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 SubmitSourceBuffer failed");
    }

    submitted_frames_ += plan.value.frame_count;
    return Result<void>::success();
}

Result<void> XAudio2Ps1AudioHost::set_volume(float gain) noexcept {
    if (!source_voice_) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 source voice is unavailable");
    }
    if (!(gain >= 0.0f && gain <= 1.0f)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "XAudio2 gain must be between 0.0 and 1.0");
    }
    const auto hr = source_voice_->SetVolume(gain, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "XAudio2 source voice volume update failed");
    }
    return Result<void>::success();
}

std::uint64_t XAudio2Ps1AudioHost::submitted_frames() const noexcept {
    return submitted_frames_;
}

} // namespace jojo
#endif
