#include "AlsaCapture.h"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <unistd.h>

AlsaCapture::AlsaCapture(const std::string& device_name,
                         snd_pcm_format_t format,
                         unsigned int sample_rate,
                         unsigned int channels,
                         LockFreeRingBuffer<float>& ring_buffer)
    : device_name_(device_name),
      format_(format),
      sample_rate_(sample_rate),
      channels_(channels),
      ring_buffer_(ring_buffer),
      capture_handle_(nullptr),
      is_running_(false),
      period_frames_(1024) {}

AlsaCapture::~AlsaCapture() {
    stop();
}

bool AlsaCapture::start() {
    int err;
    snd_pcm_hw_params_t* hw_params;

    // 1. Gerät im Capture-Modus öffnen
    if ((err = snd_pcm_open(&capture_handle_, device_name_.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "Fehler beim Öffnen des Audio-Capture-Geräts (" << device_name_ << "): " 
                  << snd_strerror(err) << std::endl;
        return false;
    }

    // 2. Hardware-Parameter allokieren und initialisieren
    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(capture_handle_, hw_params);

    // 3. Parameter setzen: Interleaved, Format, Channels, Rate
    snd_pcm_hw_params_set_access(capture_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle_, hw_params, format_);
    snd_pcm_hw_params_set_channels(capture_handle_, hw_params, channels_);

    unsigned int exact_rate = sample_rate_;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(capture_handle_, hw_params, &exact_rate, &dir);
    if (exact_rate != sample_rate_) {
        std::cerr << "Warnung: Angefragte Sample-Rate " << sample_rate_ 
                  << " Hz wird nicht exakt unterstützt. Nutze " << exact_rate << " Hz." << std::endl;
    }

    // Periodengröße setzen (z.B. 1024 Frames)
    snd_pcm_hw_params_set_period_size_near(capture_handle_, hw_params, &period_frames_, &dir);

    // 4. Parameter auf das ALSA-Device anwenden
    if ((err = snd_pcm_hw_params(capture_handle_, hw_params)) < 0) {
        std::cerr << "Fehler beim Setzen der HW-Parameter: " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hw_params);
        return false;
    }

    snd_pcm_hw_params_free(hw_params);

    // Gerät vorbereiten
    if ((err = snd_pcm_prepare(capture_handle_)) < 0) {
        std::cerr << "Fehler beim Vorbereiten des Audio-Interfaces: " << snd_strerror(err) << std::endl;
        return false;
    }

    // Thread starten
    is_running_.store(true, std::memory_order_release);
    capture_thread_ = std::thread(&AlsaCapture::captureLoop, this);

    std::cout << "ALSA Capture gestartet auf " << device_name_ << " mit " 
              << exact_rate << " Hz, " << channels_ << " Kanälen." << std::endl;

    return true;
}

void AlsaCapture::stop() {
    if (is_running_.load(std::memory_order_acquire)) {
        is_running_.store(false, std::memory_order_release);
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
    }
    
    if (capture_handle_) {
        snd_pcm_drain(capture_handle_);
        snd_pcm_close(capture_handle_);
        capture_handle_ = nullptr;
    }
}

void AlsaCapture::recover_from_xrun(int err) {
    if (err == -EPIPE) {
        // Overrun: Der Puffer ist vollgelaufen.
        std::cerr << "Capture Overrun (-EPIPE): Resetting..." << std::endl;
        snd_pcm_prepare(capture_handle_);
    } else if (err == -ESTRPIPE) {
        // Suspend
        while ((err = snd_pcm_resume(capture_handle_)) == -EAGAIN) {
            usleep(100);
        }
        if (err < 0) {
            snd_pcm_prepare(capture_handle_);
        }
    } else {
        std::cerr << "ALSA Read-Fehler: " << snd_strerror(err) << std::endl;
    }
}

void AlsaCapture::captureLoop() {
    // Puffer-Größe in Bytes ermitteln (abhängig vom Format)
    int bytes_per_sample = snd_pcm_format_physical_width(format_) / 8;
    std::vector<uint8_t> raw_buffer(period_frames_ * channels_ * bytes_per_sample);
    std::vector<float> dsp_buffer(period_frames_ * channels_);

    while (is_running_.load(std::memory_order_relaxed)) {
        int frames_read = snd_pcm_readi(capture_handle_, raw_buffer.data(), period_frames_);

        if (frames_read < 0) {
            recover_from_xrun(frames_read);
            continue;
        }

        // Format-Konvertierung in 32-Bit Float (-1.0 bis +1.0)
        int total_samples = frames_read * channels_;
        
        if (format_ == SND_PCM_FORMAT_S16_LE) {
            const int16_t* pcm16 = reinterpret_cast<const int16_t*>(raw_buffer.data());
            for (int i = 0; i < total_samples; ++i) {
                dsp_buffer[i] = pcm16[i] / 32768.0f;
            }
        } else if (format_ == SND_PCM_FORMAT_S32_LE) {
            const int32_t* pcm32 = reinterpret_cast<const int32_t*>(raw_buffer.data());
            for (int i = 0; i < total_samples; ++i) {
                dsp_buffer[i] = pcm32[i] / 2147483648.0f;
            }
        } else if (format_ == SND_PCM_FORMAT_S24_LE) {
            // ALSA padded S24_LE typischerweise auf 32 Bit (untere 24 Bit genutzt)
            const int32_t* pcm24 = reinterpret_cast<const int32_t*>(raw_buffer.data());
            for (int i = 0; i < total_samples; ++i) {
                dsp_buffer[i] = (pcm24[i] << 8) / 2147483648.0f;
            }
        }

        // In den Lock-Free Ringbuffer pushen
        // Wenn der Buffer voll ist, droppen wir die Samples hier in der Capture-Schleife.
        if (!ring_buffer_.push(dsp_buffer.data(), total_samples)) {
            // Optionale Warnung, falls Overrun im Ringbuffer passiert
            // std::cerr << "Ringbuffer Overrun: Daten verworfen." << std::endl;
        }
    }
}
