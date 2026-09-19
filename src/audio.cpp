#include "audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "miniaudio.h"

namespace ss {

struct AudioEngine::Deck {
  ma_decoder dec{}; bool loaded = false, playing = false, ended = false;
  std::vector<uint8_t> data;
  float gain = 0, target = 0; uint64_t rampLeft = 0;
  uint64_t frames = 0, total = 0;
};

struct AudioEngine::Impl {
  ma_device device{}; bool deviceUp = false;
  std::mutex m; Deck decks[2];
  std::vector<float> scratch;
  static constexpr size_t RING = 1 << 15;
  std::vector<float> ring = std::vector<float>(RING, 0.f);
  std::atomic<size_t> wpos{ 0 };
  std::atomic<float>* volume = nullptr;
};

static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frameCount) {
  auto* impl = static_cast<AudioEngine::Impl*>(dev->pUserData);
  float* o = static_cast<float*>(out);
  std::memset(o, 0, sizeof(float) * frameCount * 2);
  std::lock_guard<std::mutex> g(impl->m);
  impl->scratch.resize(size_t(frameCount) * 2);
  for (auto& d : impl->decks) {
    if (!d.loaded || !d.playing || d.ended) continue;
    ma_uint64 got = 0;
    ma_result r = ma_decoder_read_pcm_frames(&d.dec, impl->scratch.data(), frameCount, &got);
    if (r != MA_SUCCESS && r != MA_AT_END) got = 0;
    for (ma_uint64 i = 0; i < got; i++) {
      if (d.rampLeft > 0) { d.gain += (d.target - d.gain) / float(d.rampLeft); d.rampLeft--; } else d.gain = d.target;
      o[2 * i] += impl->scratch[2 * i] * d.gain; o[2 * i + 1] += impl->scratch[2 * i + 1] * d.gain;
    }
    d.frames += got;
    if (got < frameCount) d.ended = true;
  }
  float vol = impl->volume ? impl->volume->load() : 1.f;
  size_t w = impl->wpos.load(std::memory_order_relaxed);
  for (ma_uint32 i = 0; i < frameCount; i++) {
    o[2 * i] *= vol; o[2 * i + 1] *= vol;
    impl->ring[w % AudioEngine::Impl::RING] = 0.5f * (o[2 * i] + o[2 * i + 1]) / std::max(vol, 1e-3f);   // analyser sits before the volume
    w++;
  }
  impl->wpos.store(w, std::memory_order_release);
}

AudioEngine::AudioEngine() : impl_(new Impl) { impl_->volume = &volume_; }
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(std::string& err) {
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = 0;
  cfg.dataCallback = dataCallback; cfg.pUserData = impl_.get(); cfg.periodSizeInMilliseconds = 10;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) { err = "no playback device (PipeWire/Pulse/ALSA)"; return false; }
  sampleRate_ = int(impl_->device.sampleRate);
  if (ma_device_start(&impl_->device) != MA_SUCCESS) { err = "audio device did not start"; ma_device_uninit(&impl_->device); return false; }
  impl_->deviceUp = true; ready_ = true;
  return true;
}

void AudioEngine::shutdown() {
  if (!impl_) return;
  if (impl_->deviceUp) { ma_device_uninit(&impl_->device); impl_->deviceUp = false; }
  for (int i = 0; i < 2; i++) unload(i);
  ready_ = false;
}

bool AudioEngine::load(int i, std::vector<uint8_t> bytes, std::string& err) {
  Deck fresh; fresh.data = std::move(bytes);
  ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 2, ma_uint32(sampleRate_));
  if (ma_decoder_init_memory(fresh.data.data(), fresh.data.size(), &dc, &fresh.dec) != MA_SUCCESS) { err = "could not decode audio"; return false; }
  ma_uint64 len = 0; ma_decoder_get_length_in_pcm_frames(&fresh.dec, &len);
  fresh.total = len; fresh.loaded = true;
  std::lock_guard<std::mutex> g(impl_->m);
  Deck& d = impl_->decks[i];
  if (d.loaded) ma_decoder_uninit(&d.dec);
  d.data = std::move(fresh.data); d.dec = fresh.dec; d.total = fresh.total; d.loaded = true;
  d.ended = false; d.playing = false; d.frames = 0; d.gain = 0; d.target = 0; d.rampLeft = 0;
  return true;
}

void AudioEngine::unload(int i) {
  std::lock_guard<std::mutex> g(impl_->m);
  Deck& d = impl_->decks[i];
  if (d.loaded) ma_decoder_uninit(&d.dec);
  d = Deck{};
}
void AudioEngine::setGain(int i, float target, double rampSeconds) {
  std::lock_guard<std::mutex> g(impl_->m);
  Deck& d = impl_->decks[i]; d.target = target;
  d.rampLeft = rampSeconds > 0 ? uint64_t(rampSeconds * sampleRate_) : 0;
  if (!d.rampLeft) d.gain = target;
}
void AudioEngine::setPlaying(int i, bool p) { std::lock_guard<std::mutex> g(impl_->m); impl_->decks[i].playing = p; }
bool AudioEngine::playing(int i) { std::lock_guard<std::mutex> g(impl_->m); return impl_->decks[i].playing; }
bool AudioEngine::loaded(int i) { std::lock_guard<std::mutex> g(impl_->m); return impl_->decks[i].loaded; }
bool AudioEngine::ended(int i) { std::lock_guard<std::mutex> g(impl_->m); return impl_->decks[i].ended; }
double AudioEngine::position(int i) { std::lock_guard<std::mutex> g(impl_->m); return double(impl_->decks[i].frames) / sampleRate_; }
double AudioEngine::duration(int i) { std::lock_guard<std::mutex> g(impl_->m); return double(impl_->decks[i].total) / sampleRate_; }

void AudioEngine::tapLatest(float* out, int n) {
  size_t w = impl_->wpos.load(std::memory_order_acquire);
  for (int k = 0; k < n; k++) out[k] = impl_->ring[(w + Impl::RING - n + k) % Impl::RING];
}

} // namespace ss
