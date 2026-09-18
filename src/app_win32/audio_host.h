#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_set>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>

namespace jojo {

struct XAudio2PcmPlan {
    std::uint32_t sample_rate{44100u};
    std::uint16_t channels{2u};
    std::uint16_t bits_per_sample{16u};
    std::uint16_t block_align{4u};
    std::uint32_t average_bytes_per_second{176400u};
    std::size_t frame_count{};
    std::size_t byte_size{};
    const std::int16_t* samples{};
};

[[nodiscard]] Result<XAudio2PcmPlan> make_xaudio2_pcm_plan(
    std::span<const std::int16_t> interleaved_stereo) noexcept;
[[nodiscard]] float xaudio2_gain_from_percent(int percent) noexcept;

class XAudio2Ps1AudioHost {
public:
    ~XAudio2Ps1AudioHost();

    XAudio2Ps1AudioHost(const XAudio2Ps1AudioHost&) = delete;
    XAudio2Ps1AudioHost& operator=(const XAudio2Ps1AudioHost&) = delete;
    XAudio2Ps1AudioHost(XAudio2Ps1AudioHost&&) = delete;
    XAudio2Ps1AudioHost& operator=(XAudio2Ps1AudioHost&&) = delete;

    [[nodiscard]] static Result<std::unique_ptr<XAudio2Ps1AudioHost>> create();
    [[nodiscard]] Result<void> submit(
        std::span<const std::int16_t> interleaved_stereo);
    [[nodiscard]] Result<void> set_volume(float gain) noexcept;
    [[nodiscard]] std::uint64_t submitted_frames() const noexcept;

private:
    struct OwnedBuffer {
        std::vector<std::int16_t> samples;
    };

    class VoiceCallback final : public IXAudio2VoiceCallback {
    public:
        void track(OwnedBuffer* buffer);
        void discard(OwnedBuffer* buffer) noexcept;
        void release_all() noexcept;

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void* context) override;
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

    private:
        std::mutex mutex_;
        std::unordered_set<OwnedBuffer*> outstanding_;
    };

    XAudio2Ps1AudioHost() = default;

    Microsoft::WRL::ComPtr<IXAudio2> engine_{};
    IXAudio2MasteringVoice* mastering_voice_{};
    IXAudio2SourceVoice* source_voice_{};
    VoiceCallback callback_{};
    std::uint64_t submitted_frames_{};
};

} // namespace jojo
#endif
