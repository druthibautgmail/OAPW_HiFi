#ifndef ALSA_PLAYBACK_H
#define ALSA_PLAYBACK_H

#include <alsa/asoundlib.h>
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include "LockFreeRingBuffer.h"

class AlsaPlayback {
public:
    // Konstruktor: Konfiguriert das ALSA-Playback-Device (Hardware DAC)
    // @param device_name z.B. "hw:Katana,0" oder "hw:Sharkoon,0"
    // @param format Das gewünschte Ausgabeformat des DACs (z.B. SND_PCM_FORMAT_S32_LE)
    // @param sample_rate Samplingfrequenz in Hz (muss zur Capture-Rate passen)
    // @param channels Anzahl der Kanäle (Standard: 2 für Stereo)
    // @param ring_buffer Referenz auf den geteilten Lock-free Ringbuffer
    AlsaPlayback(const std::string& device_name,
                 snd_pcm_format_t format,
                 unsigned int sample_rate,
                 unsigned int channels,
                 LockFreeRingBuffer<float>& ring_buffer);

    ~AlsaPlayback();

    // Optional: DSP-Callback setzen (z.B. für den RACE Algorithmus)
    // Wird im Playback-Thread aufgerufen, nachdem die Daten aus dem Ringbuffer 
    // geholt wurden, aber bevor sie an den Hardware-DAC gesendet werden.
    // @param float* buffer - Die Float32 Audiodaten (-1.0 bis 1.0)
    // @param size_t frames - Anzahl der Frames (Frames * Channels = Samples)
    void set_dsp_callback(std::function<void(float*, size_t)> callback);

    // Öffnet das ALSA-Gerät, setzt die Hardware-Parameter und startet den Thread.
    bool start();

    // Signalisiert dem Thread das Ende, wartet auf ihn und schließt das Gerät.
    void stop();

private:
    std::string device_name_;
    snd_pcm_format_t format_;
    unsigned int sample_rate_;
    unsigned int channels_;
    
    LockFreeRingBuffer<float>& ring_buffer_;
    
    // Callback für die RACE DSP-Engine
    std::function<void(float*, size_t)> dsp_callback_;

    snd_pcm_t* playback_handle_;
    std::atomic<bool> is_running_;
    std::thread playback_thread_;

    snd_pcm_uframes_t period_frames_;

    // Die Worker-Schleife für den DAC. Hier greift auch das "Re-Centering".
    void playbackLoop();

    // Fängt -EPIPE (Underrun) und Suspend-Events ab.
    void recover_from_xrun(int err);
};

#endif // ALSA_PLAYBACK_H
