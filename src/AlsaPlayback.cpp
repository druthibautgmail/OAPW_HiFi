#include "AlsaPlayback.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <unistd.h>

AlsaPlayback::AlsaPlayback(const std::string& device_name,
                           snd_pcm_format_t format,
                           unsigned int sample_rate,
                           unsigned int channels,
                           LockFreeRingBuffer<float>& ring_buffer)
    : device_name_(device_name),
      format_(format),
      sample_rate_(sample_rate),
      channels_(channels),
      ring_buffer_(ring_buffer),
      dsp_callback_(nullptr),
      playback_handle_(nullptr),
      is_running_(false),
      period_frames_(1024) {}

AlsaPlayback::~AlsaPlayback() {
    stop();
}

void AlsaPlayback::set_dsp_callback(std::function<void(float*, size_t)> callback) {
    dsp_callback_ = callback;
}

bool AlsaPlayback::start() {
    int err;
    snd_pcm_hw_params_t* hw_params;

    // 1. Gerät im Playback-Modus (DAC) öffnen
    if ((err = snd_pcm_open(&playback_handle_, device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        std::cerr << "Fehler beim Öffnen des Playback-Geräts (" << device_name_ << "): " 
                  << snd_strerror(err) << std::endl;
        return false;
    }

    // 2. Hardware-Parameter konfigurieren
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(playback_handle_, hw_params);

    snd_pcm_hw_params_set_access(playback_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle_, hw_params, format_);
    snd_pcm_hw_params_set_channels(playback_handle_, hw_params, channels_);

    unsigned int exact_rate = sample_rate_;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(playback_handle_, hw_params, &exact_rate, &dir);
    if (exact_rate != sample_rate_) {
        std::cerr << "Warnung: DAC " << device_name_ << " nutzt " << exact_rate 
                  << " Hz anstelle der angefragten " << sample_rate_ << " Hz." << std::endl;
    }

    snd_pcm_hw_params_set_period_size_near(playback_handle_, hw_params, &period_frames_, &dir);

    if ((err = snd_pcm_hw_params(playback_handle_, hw_params)) < 0) {
        std::cerr << "Fehler beim Setzen der DAC HW-Parameter: " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hw_params);
        return false;
    }
    snd_pcm_hw_params_free(hw_params);

    if ((err = snd_pcm_prepare(playback_handle_)) < 0) {
        std::cerr << "Fehler bei snd_pcm_prepare (Playback): " << snd_strerror(err) << std::endl;
        return false;
    }

    // Thread starten
    is_running_.store(true, std::memory_order_release);
    playback_thread_ = std::thread(&AlsaPlayback::playbackLoop, this);

    std::cout << "ALSA Playback gestartet auf " << device_name_ << " mit " 
              << exact_rate << " Hz, " << channels_ << " Kanälen." << std::endl;

    return true;
}

void AlsaPlayback::stop() {
    if (is_running_.load(std::memory_order_acquire)) {
        is_running_.store(false, std::memory_order_release);
        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }
    }
    
    if (playback_handle_) {
        snd_pcm_drain(playback_handle_);
        snd_pcm_close(playback_handle_);
        playback_handle_ = nullptr;
    }
}

void AlsaPlayback::recover_from_xrun(int err) {
    if (err == -EPIPE) {
        // Underrun: Wir haben dem DAC nicht schnell genug Daten geliefert.
        std::cerr << "Playback Underrun (-EPIPE). DAC fordert Daten. Resetting..." << std::endl;
        snd_pcm_prepare(playback_handle_);
    } else if (err == -ESTRPIPE) {
        // Suspend
        while ((err = snd_pcm_resume(playback_handle_)) == -EAGAIN) {
            usleep(100);
        }
        if (err < 0) {
            snd_pcm_prepare(playback_handle_);
        }
    } else {
        std::cerr << "ALSA Write-Fehler: " << snd_strerror(err) << std::endl;
    }
}

void AlsaPlayback::playbackLoop() {
    int bytes_per_sample = snd_pcm_format_physical_width(format_) / 8;
    int total_samples = period_frames_ * channels_;
    
    std::vector<float> dsp_buffer(total_samples);
    std::vector<uint8_t> raw_buffer(total_samples * bytes_per_sample, 0);

    while (is_running_.load(std::memory_order_relaxed)) {
        // 1. Daten aus dem Ringbuffer holen
        if (!ring_buffer_.pop(dsp_buffer.data(), total_samples)) {
            // RINGBUFFER UNDERRUN: Der MediaPlayer pausiert oder der Clock-Drift hat den Puffer geleert.
            // Strategie 1: Re-Centering aktivieren und digitale Stille (Zeroes) an den DAC senden,
            // um den Hardware-Takt am Laufen zu halten, ohne Knackser zu erzeugen.
            ring_buffer_.reset();
            std::fill(raw_buffer.begin(), raw_buffer.end(), 0);
        } else {
            // 2. RACE DSP Algorithmus anwenden (falls ein Callback konfiguriert ist)
            if (dsp_callback_) {
                dsp_callback_(dsp_buffer.data(), period_frames_);
            }

            // 3. Float32 (-1.0 bis 1.0) zurück in das DAC-Hardware-Format konvertieren
            if (format_ == SND_PCM_FORMAT_S16_LE) {
                int16_t* pcm16 = reinterpret_cast<int16_t*>(raw_buffer.data());
                for (int i = 0; i < total_samples; ++i) {
                    // Hard-Clipping verhindern
                    float clamped = std::clamp(dsp_buffer[i], -1.0f, 1.0f);
                    pcm16[i] = static_cast<int16_t>(clamped * 32767.0f);
                }
            } else if (format_ == SND_PCM_FORMAT_S32_LE) {
                int32_t* pcm32 = reinterpret_cast<int32_t*>(raw_buffer.data());
                for (int i = 0; i < total_samples; ++i) {
                    float clamped = std::clamp(dsp_buffer[i], -1.0f, 1.0f);
                    pcm32[i] = static_cast<int32_t>(clamped * 2147483647.0f);
                }
            } else if (format_ == SND_PCM_FORMAT_S24_LE) {
                // S24_LE (in 32-bit Container gepackt, je nach ALSA padding)
                int32_t* pcm24 = reinterpret_cast<int32_t*>(raw_buffer.data());
                for (int i = 0; i < total_samples; ++i) {
                    float clamped = std::clamp(dsp_buffer[i], -1.0f, 1.0f);
                    // Den Wert skalieren und entsprechend shiften
                    pcm24[i] = static_cast<int32_t>(clamped * 2147483647.0f) >> 8;
                }
            }
        }

        // 4. Synchrones Schreiben auf den Hardware-DAC
        int frames_written = snd_pcm_writei(playback_handle_, raw_buffer.data(), period_frames_);

        if (frames_written < 0) {
            // Tritt meist auf, wenn das System überlastet ist und snd_pcm_writei zu spät aufgerufen wird
            recover_from_xrun(frames_written);
        } else if (frames_written != (int)period_frames_) {
            std::cerr << "Warnung: Short write auf DAC (" << frames_written 
                      << "/" << period_frames_ << ")" << std::endl;
        }
    }
}
