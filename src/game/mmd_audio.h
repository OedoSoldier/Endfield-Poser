#pragma once
#include "math/mmd_audio_sync.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <xaudio2.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace mmd {
inline void AudioCheck(HRESULT hr, const char *operation) {
  if (FAILED(hr)) {
    char code[24];
    sprintf_s(code, " (0x%08lX)", static_cast<unsigned long>(hr));
    throw std::runtime_error(std::string(operation) + code);
  }
}
struct AudioClip {
  WAVEFORMATEX format{};
  std::vector<BYTE> pcm;
  double duration() const {
    return format.nAvgBytesPerSec ? double(pcm.size()) / format.nAvgBytesPerSec
                                  : 0;
  }
};

inline std::shared_ptr<const AudioClip>
DecodeWavDirect(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return nullptr;

  char riff[4];
  if (!file.read(riff, 4) || std::memcmp(riff, "RIFF", 4) != 0) return nullptr;

  uint32_t riffSize = 0;
  if (!file.read(reinterpret_cast<char*>(&riffSize), 4)) return nullptr;

  char wave[4];
  if (!file.read(wave, 4) || std::memcmp(wave, "WAVE", 4) != 0) return nullptr;

  WAVEFORMATEX fmt{};
  bool hasFmt = false;
  std::vector<BYTE> pcmData;
  constexpr size_t maxBytes = 256u * 1024u * 1024u;

  while (file) {
    char chunkId[4];
    uint32_t chunkSize = 0;
    if (!file.read(chunkId, 4)) break;
    if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

    if (std::memcmp(chunkId, "fmt ", 4) == 0) {
      if (chunkSize < 16) return nullptr;
      if (!file.read(reinterpret_cast<char*>(&fmt), 16)) return nullptr;
      if (chunkSize > 16) {
        file.seekg(chunkSize - 16, std::ios::cur);
      }
      hasFmt = true;
    } else if (std::memcmp(chunkId, "data", 4) == 0) {
      if (chunkSize > maxBytes) {
        throw std::runtime_error("Decoded music exceeds 256 MiB (about 23 minutes)");
      }
      pcmData.resize(chunkSize);
      if (!file.read(reinterpret_cast<char*>(pcmData.data()), chunkSize)) return nullptr;
      if (chunkSize % 2 != 0) {
        file.seekg(1, std::ios::cur);
      }
      break;
    } else {
      file.seekg(chunkSize + (chunkSize % 2), std::ios::cur);
    }
  }

  if (!hasFmt || pcmData.empty()) return nullptr;
  if (fmt.wFormatTag != WAVE_FORMAT_PCM && fmt.wFormatTag != 3 /* WAVE_FORMAT_IEEE_FLOAT */) {
    return nullptr;
  }
  if (fmt.nBlockAlign == 0 || pcmData.size() % fmt.nBlockAlign != 0) return nullptr;

  auto clip = std::make_shared<AudioClip>();
  clip->format = fmt;
  clip->pcm = std::move(pcmData);
  return clip;
}

// This function runs only on the file worker; it never accesses Unity objects.
inline std::shared_ptr<const AudioClip>
DecodeAudio(const std::filesystem::path &path,
            const std::atomic<bool> &cancel) {
  // If the file is a standard WAV, directly decode it first.
  // This bypasses Windows Media Foundation / Media Player dependencies under Wine / Proton.
  try {
    auto directClip = DecodeWavDirect(path);
    if (directClip && !directClip->pcm.empty()) {
      return directClip;
    }
  } catch (...) {
    // If direct WAV reading throws, fall through to Media Foundation or let error propagate
  }
  struct MediaScope {
    bool com = false, mf = false;
    MediaScope() {
      HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      AudioCheck(hr, "Audio COM initialization failed");
      com = true;
      hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
      if (FAILED(hr)) {
        CoUninitialize();
        com = false;
        AudioCheck(hr, "MFStartup failed");
      }
      mf = true;
    }
    ~MediaScope() {
      if (mf)
        MFShutdown();
      if (com)
        CoUninitialize();
    }
  } scope;
  using Microsoft::WRL::ComPtr;
  ComPtr<IMFSourceReader> reader;
  AudioCheck(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader),
             "Cannot open music (try WAV/MP3/M4A)");
  AudioCheck(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE),
             "Audio stream selection failed");
  AudioCheck(
      reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE),
      "No audio stream");
  auto clip = std::make_shared<AudioClip>();
  clip->format = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
  ComPtr<IMFMediaType> type;
  AudioCheck(MFCreateMediaType(&type), "Audio type creation failed");
  AudioCheck(MFInitMediaTypeFromWaveFormatEx(type.Get(), &clip->format,
                                             sizeof(WAVEFORMATEX)),
             "Audio format failed");
  AudioCheck(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                         nullptr, type.Get()),
             "Cannot decode to stereo PCM");
  constexpr size_t maxBytes = 256u * 1024u * 1024u;
  for (;;) {
    if (cancel.load())
      throw std::runtime_error("Music loading cancelled");
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    AudioCheck(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                  nullptr, &flags, nullptr, &sample),
               "Music decoding failed");
    if (flags &
        (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
      throw std::runtime_error("Music stream changed format or is damaged");
    if (sample) {
      ComPtr<IMFMediaBuffer> buffer;
      AudioCheck(sample->ConvertToContiguousBuffer(&buffer),
                 "Audio sample conversion failed");
      BYTE *bytes = nullptr;
      DWORD count = 0;
      AudioCheck(buffer->Lock(&bytes, nullptr, &count),
                 "Audio buffer lock failed");
      struct Unlock {
        IMFMediaBuffer *p;
        ~Unlock() { p->Unlock(); }
      } unlock{buffer.Get()};
      if (count > maxBytes - clip->pcm.size())
        throw std::runtime_error(
            "Decoded music exceeds 256 MiB (about 23 minutes)");
      clip->pcm.insert(clip->pcm.end(), bytes, bytes + count);
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      break;
  }
  if (clip->pcm.empty() || clip->pcm.size() % clip->format.nBlockAlign)
    throw std::runtime_error("Music has no complete audio samples");
  return clip;
}

class AudioPlayer {
  IXAudio2 *engine_ = nullptr;
  IXAudio2MasteringVoice *master_ = nullptr;
  IXAudio2SourceVoice *voice_ = nullptr;
  CO_MTA_USAGE_COOKIE comCookie_ = nullptr;
  // Buffer lifetime must extend past DestroyVoice; the engine reads it async.
  std::shared_ptr<const AudioClip> clip_;
  bool running_ = false;
  double start_ = 0, lastMotion_ = -1;
  void destroyVoice() {
    if (voice_) {
      voice_->DestroyVoice();
      voice_ = nullptr;
    }
    running_ = false;
  }
  void prepareEngine() {
    if (engine_)
      return;
    // Playback commands can originate on Unity, GUI or fallback threads.
    // A process MTA reference has no CoUninitialize thread-affinity
    // requirement.
    AudioCheck(CoIncrementMTAUsage(&comCookie_),
               "Audio COM initialization failed");
    AudioCheck(XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR),
               "Audio output initialization failed");
    HRESULT hr = engine_->CreateMasteringVoice(&master_);
    if (FAILED(hr)) {
      engine_->Release();
      engine_ = nullptr;
      AudioCheck(hr, "Audio output device unavailable");
    }
  }

public:
  ~AudioPlayer() { close(); }
  AudioPlayer() = default;
  AudioPlayer(const AudioPlayer &) = delete;
  AudioPlayer &operator=(const AudioPlayer &) = delete;
  void stop() {
    if (voice_ && running_)
      AudioCheck(voice_->Stop(), "Music pause failed");
    running_ = false;
    lastMotion_ = -1;
  }
  void close() {
    destroyVoice();
    if (master_) {
      master_->DestroyVoice();
      master_ = nullptr;
    }
    if (engine_) {
      engine_->Release();
      engine_ = nullptr;
    }
    if (comCookie_) {
      CoDecrementMTAUsage(comCookie_);
      comCookie_ = nullptr;
    }
    lastMotion_ = -1;
  }
  void setClip(std::shared_ptr<const AudioClip> clip) {
    close();
    clip_ = std::move(clip);
  }
  const std::shared_ptr<const AudioClip> &clip() const { return clip_; }
  bool running() const { return running_; }
  double position() const {
    if (!voice_ || !clip_)
      return 0;
    XAUDIO2_VOICE_STATE state{};
    voice_->GetState(&state);
    if (!state.BuffersQueued)
      return clip_->duration();
    return start_ + double(state.SamplesPlayed) / clip_->format.nSamplesPerSec;
  }
  void sync(const Timeline &t, bool active, bool enabled, double offset,
            float volume) {
    if (!clip_)
      return;
    bool wrapped = lastMotion_ >= 0 && t.seconds + 1e-6 < lastMotion_;
    lastMotion_ = t.seconds;
    auto p = PlanAudio(t, active, enabled, clip_->duration(), offset, running_,
                       position(), wrapped);
    if (p.action == AudioAction::Silent) {
      stop();
      return;
    }
    if (p.action == AudioAction::Restart) {
      prepareEngine();
      // Recreate a stopped source rather than relying on asynchronous Flush.
      destroyVoice();
      AudioCheck(engine_->CreateSourceVoice(&voice_, &clip_->format, 0, 2.f),
                 "Music voice creation failed");
      size_t frame = size_t(p.seconds * clip_->format.nSamplesPerSec);
      size_t byte = frame * clip_->format.nBlockAlign;
      start_ = double(frame) / clip_->format.nSamplesPerSec;
      XAUDIO2_BUFFER buffer{};
      buffer.Flags = XAUDIO2_END_OF_STREAM;
      buffer.AudioBytes = UINT32(clip_->pcm.size() - byte);
      buffer.pAudioData = clip_->pcm.data() + byte;
      AudioCheck(voice_->SubmitSourceBuffer(&buffer),
                 "Music buffer submission failed");
    }
    AudioCheck(voice_->SetVolume((std::max)(0.f, (std::min)(1.f, volume))),
               "Music volume failed");
    AudioCheck(voice_->SetFrequencyRatio(float(t.speed)), "Music speed failed");
    if (!running_) {
      AudioCheck(voice_->Start(), "Music playback failed");
      running_ = true;
    }
  }
};
} // namespace mmd
