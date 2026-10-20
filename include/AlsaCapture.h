#ifndef ALSA_CAPTURE_H
#define ALSA_CAPTURE_H

#include <alsa/asoundlib.h>
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include "LockFreeRingBuffer.h"

class AlsaCapture {
public:
    // Konstruktor: Konfiguriert das ALSA-Capture-Device.
    // @param device_name z.B. "hw:Loopback,1,0"
    // @param format Das vom MediaPlayer gelieferte Format (z.B. SND_PCM_FORMAT_S16_LE)
    // @param sample_rate Samplingfrequenz in Hz (z.B. 44100 oder 192000)
    // @param channels Anzahl der Kanäle (Standard: 2 für Stereo)
    // @param ring_buffer Referenz auf den geteilten Lock-free Ringbuffer
    AlsaCapture(const std::string& device_name,
                snd_pcm_format_t format,
                unsigned int sample_rate,
                unsigned int channels,
                LockFreeRingBuffer<float>& ring_buffer);

    ~AlsaCapture();

    // Öffnet das ALSA-Gerät, setzt die Hardware-Parameter und startet den Capture-Thread.
    bool start();

    // Signalisiert dem Thread das Ende, wartet auf ihn (join) und schließt das Gerät.
    void stop();

private:
    std::string device_name_;
    snd_pcm_format_t format_;
    unsigned int sample_rate_;
    unsigned int channels_;
    
    LockFreeRingBuffer<float>& ring_buffer_;

    snd_pcm_t* capture_handle_;
    std::atomic<bool> is_running_;
    std::thread capture_thread_;

    snd_pcm_uframes_t period_frames_;

    // Die Worker-Schleife, die asynchron läuft und snd_pcm_readi aufruft
    void captureLoop();

    // Fängt -EPIPE (Overrun) und Suspend-Events (-ESTRPIPE) sauber ab
    void recover_from_xrun(int err);
};

#endif // ALSA_CAPTURE_H
