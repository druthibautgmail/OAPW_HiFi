#include "RACEDspEngine.h"
#include <iostream>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Eigener, extrem schneller FFT Algorithmus (64-Bit double) ---
void computeFFT(std::vector<std::complex<double>>& data) {
    size_t n = data.size();
    if (n <= 1) return;
    
    // Bit-Reversal
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    
    // Cooley-Tukey
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1, 0);
            for (size_t j = 0; j < len / 2; j++) {
                std::complex<double> u = data[i + j];
                std::complex<double> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
// ------------------------------------------------------

// --- BiquadFilter Implementierung (64-Bit / RBJ Cookbook) ---
BiquadFilter::BiquadFilter() : z1(0.0), z2(0.0), sampleRate(192000) {
    setParameters(1000.0, 0.707, 0.0, 192000); 
}

// Peaking EQ
void BiquadFilter::setParameters(double frequency, double qFactor, double gain, int sr) {
    freq = frequency; q = qFactor; gainDb = gain; sampleRate = sr;
    
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * q);
    
    double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = (-2.0 * std::cos(w0)) / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = (-2.0 * std::cos(w0)) / a0;
    a2 = (1.0 - alpha / A) / a0;
}

// LowPass Filter für RACE-Höhenbegrenzung
void BiquadFilter::setLowPass(double frequency, double qFactor, int sr) {
    freq = frequency; q = qFactor; sampleRate = sr;
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * q);
    
    double a0 = 1.0 + alpha;
    b0 = ((1.0 - std::cos(w0)) / 2.0) / a0;
    b1 = (1.0 - std::cos(w0)) / a0;
    b2 = ((1.0 - std::cos(w0)) / 2.0) / a0;
    a1 = (-2.0 * std::cos(w0)) / a0;
    a2 = (1.0 - alpha) / a0;
}

// HighPass Filter für RACE-Bassbegrenzung
void BiquadFilter::setHighPass(double frequency, double qFactor, int sr) {
    freq = frequency; q = qFactor; sampleRate = sr;
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * q);
    
    double a0 = 1.0 + alpha;
    b0 = ((1.0 + std::cos(w0)) / 2.0) / a0;
    b1 = -(1.0 + std::cos(w0)) / a0;
    b2 = ((1.0 + std::cos(w0)) / 2.0) / a0;
    a1 = (-2.0 * std::cos(w0)) / a0;
    a2 = (1.0 - alpha) / a0;
}

double BiquadFilter::process(double in) {
    double out = in * b0 + z1;
    z1 = in * b1 - out * a1 + z2;
    z2 = in * b2 - out * a2;
    return out;
}
// ------------------------------------------

RACEDspEngine::RACEDspEngine(double initialDelayUs, double initialAttenuationDb, double initialCenterP, bool initialFreqLimit, int sr) 
    : sampleRate(sr) {
    
    volume = 1.0; 
    raceEnabled = true;
    filtersEnabled = true;
    eqEnabled = false;

    // RACE Filter Initialisierung (Kaskadiert für 24dB/Okt Flankensteilheit)
    // Q = 0.707 (Butterworth) liefert den flachsten Durchlassbereich
    double raceHpFreq = 150.0;  // Bässe unter 150Hz nicht crosstalken
    double raceLpFreq = 4000.0; // Höhen über 4000Hz unangetastet lassen
    
    for (int i = 0; i < 2; ++i) {
        lpfL[i].setLowPass(raceLpFreq, 0.707, sampleRate);
        lpfR[i].setLowPass(raceLpFreq, 0.707, sampleRate);
        hpfL[i].setHighPass(raceHpFreq, 0.707, sampleRate);
        hpfR[i].setHighPass(raceHpFreq, 0.707, sampleRate);
    }

    // EQ Initialisierung mit übergebener Samplerate
    for (int i = 0; i < 3; ++i) {
        double f = (i == 0) ? 100.0 : (i == 1) ? 1000.0 : 5000.0;
        eqL[i].setParameters(f, 0.707, 0.0, sampleRate);
        eqR[i].setParameters(f, 0.707, 0.0, sampleRate);
    }

    spectrumBuffer.resize(1024, 0.0);
    spectrumIndex = 0;

    setParameters(initialDelayUs, initialAttenuationDb, initialCenterP, initialFreqLimit);
}

RACEDspEngine::~RACEDspEngine() {}

void RACEDspEngine::setParameters(double newDelayUs, double newAttenuationDb, double newCenterP, bool newFreqLimit) {
    std::lock_guard<std::mutex> lock(dspMutex); 
    
    delayUs = newDelayUs;
    attenuationDb = newAttenuationDb;
    centerP = newCenterP;
    freqLimitRACE = newFreqLimit;

    // Dynamische Berechnung des Delay-Puffers basierend auf echter Samplerate
    int calculatedDn = static_cast<int>(std::round(delayUs * sampleRate / 1000000.0));
    dn = (calculatedDn > 0) ? calculatedDn : 1;
    attenuationLinear = std::pow(10.0, attenuationDb / 20.0);

    delayBufferL.assign(dn, 0.0);
    delayBufferR.assign(dn, 0.0);
    writeIndex = 0;
}

void RACEDspEngine::setVolume(double newVolume) { std::lock_guard<std::mutex> lock(dspMutex); volume = newVolume; }
void RACEDspEngine::setRaceEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); raceEnabled = enabled; }
void RACEDspEngine::setFiltersEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); filtersEnabled = enabled; }

void RACEDspEngine::setEqEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); eqEnabled = enabled; }
void RACEDspEngine::setEqBand(int b, double f, double q, double g) {
    std::lock_guard<std::mutex> lock(dspMutex);
    if(b >= 0 && b < 3) {
        eqL[b].setParameters(f, q, g, sampleRate);
        eqR[b].setParameters(f, q, g, sampleRate);
    }
}

double RACEDspEngine::getDelayUs() { std::lock_guard<std::mutex> lock(dspMutex); return delayUs; }
double RACEDspEngine::getAttenuationDb() { std::lock_guard<std::mutex> lock(dspMutex); return attenuationDb; }
double RACEDspEngine::getCenterP() { std::lock_guard<std::mutex> lock(dspMutex); return centerP; }
bool RACEDspEngine::getFreqLimit() { std::lock_guard<std::mutex> lock(dspMutex); return freqLimitRACE; }
double RACEDspEngine::getVolume() { std::lock_guard<std::mutex> lock(dspMutex); return volume; }
bool RACEDspEngine::getRaceEnabled() { std::lock_guard<std::mutex> lock(dspMutex); return raceEnabled; }
bool RACEDspEngine::getFiltersEnabled() { std::lock_guard<std::mutex> lock(dspMutex); return filtersEnabled; }

bool RACEDspEngine::getEqEnabled() { std::lock_guard<std::mutex> lock(dspMutex); return eqEnabled; }
double RACEDspEngine::getEqFreq(int b) { std::lock_guard<std::mutex> lock(dspMutex); return (b>=0 && b<3) ? eqL[b].getFreq() : 0.0; }
double RACEDspEngine::getEqQ(int b) { std::lock_guard<std::mutex> lock(dspMutex); return (b>=0 && b<3) ? eqL[b].getQ() : 0.0; }
double RACEDspEngine::getEqGain(int b) { std::lock_guard<std::mutex> lock(dspMutex); return (b>=0 && b<3) ? eqL[b].getGain() : 0.0; }

// -------------------------------------------------------------------------
// ALTE ROUTINE (Falls weiterhin für Datei-Output benötigt)
// -------------------------------------------------------------------------
void RACEDspEngine::processSamples(std::vector<double>& interleavedSamples) {
    std::lock_guard<std::mutex> lock(dspMutex); 

    for (size_t i = 0; i + 1 < interleavedSamples.size(); i += 2) {
        double currentL = interleavedSamples[i];
        double currentR = interleavedSamples[i + 1];

        if (!raceEnabled) {
            double bypassL = currentL * volume;
            double bypassR = currentR * volume;
            
            interleavedSamples[i] = bypassL;
            interleavedSamples[i + 1] = bypassR;
            
            spectrumBuffer[spectrumIndex] = (bypassL + bypassR) * 0.5;
            spectrumIndex = (spectrumIndex + 1) % 1024;
            continue;
        }

        double lpL = currentL, lpR = currentR, hpL = currentL, hpR = currentR;

        if (filtersEnabled && freqLimitRACE) {
            for (int k = 0; k < 2; ++k) {
                hpL = hpfL[k].process(hpL);
                hpR = hpfR[k].process(hpR);
                lpL = lpfL[k].process(lpL);
                lpR = lpfR[k].process(lpR);
            }
        }

        double delayedL = delayBufferL[writeIndex];
        double delayedR = delayBufferR[writeIndex];

        double crossTalkL = attenuationLinear * (delayedR - centerP * delayedL) / (1.0 + centerP);
        double crossTalkR = attenuationLinear * (delayedL - centerP * delayedR) / (1.0 + centerP);

        delayBufferL[writeIndex] = lpL - crossTalkL;
        delayBufferR[writeIndex] = lpR - crossTalkR;

        writeIndex++;
        if (writeIndex >= dn) writeIndex = 0;

        double rawOutL = delayedL + hpL;
        double rawOutR = delayedR + hpR;

        if (eqEnabled) {
            for (int b = 0; b < 3; ++b) {
                rawOutL = eqL[b].process(rawOutL);
                rawOutR = eqR[b].process(rawOutR);
            }
        }

        double outL = rawOutL * volume;
        double outR = rawOutR * volume;
        
        outL = std::clamp(outL, -1.0, 1.0);
        outR = std::clamp(outR, -1.0, 1.0);

        spectrumBuffer[spectrumIndex] = (outL + outR) * 0.5;
        spectrumIndex = (spectrumIndex + 1) % 1024;

        interleavedSamples[i] = outL;
        interleavedSamples[i + 1] = outR;
    }
}

// -------------------------------------------------------------------------
// NEUE ALSA ROUTINE: Direktes 32-Bit Rendering (Maximale Performance)
// -------------------------------------------------------------------------
void RACEDspEngine::processFramesForAlsa(const double* inBuffer64, int32_t* outAlsaBuffer32, int frames) {
    std::lock_guard<std::mutex> lock(dspMutex); 
    
    for (int i = 0; i < frames; ++i) {
        double currentL = inBuffer64[i * 2];
        double currentR = inBuffer64[i * 2 + 1];

        if (!raceEnabled) {
            double bypassL = currentL * volume;
            double bypassR = currentR * volume;
            
            // Analyzer abgreifen
            spectrumBuffer[spectrumIndex] = (bypassL + bypassR) * 0.5;
            spectrumIndex = (spectrumIndex + 1) % 1024;

            // Clamping und Konvertierung in int32
            bypassL = std::clamp(bypassL, -1.0, 1.0);
            bypassR = std::clamp(bypassR, -1.0, 1.0);
            outAlsaBuffer32[i * 2]     = static_cast<int32_t>(bypassL * 2147483647.0);
            outAlsaBuffer32[i * 2 + 1] = static_cast<int32_t>(bypassR * 2147483647.0);
            continue;
        }

        double lpL = currentL, lpR = currentR, hpL = currentL, hpR = currentR;

        if (filtersEnabled && freqLimitRACE) {
            // Signal durch beide Biquad-Stufen schicken (4th Order Filterung)
            for (int k = 0; k < 2; ++k) {
                hpL = hpfL[k].process(hpL);
                hpR = hpfR[k].process(hpR);
                lpL = lpfL[k].process(lpL);
                lpR = lpfR[k].process(lpR);
            }
        }

        double delayedL = delayBufferL[writeIndex];
        double delayedR = delayBufferR[writeIndex];

        double crossTalkL = attenuationLinear * (delayedR - centerP * delayedL) / (1.0 + centerP);
        double crossTalkR = attenuationLinear * (delayedL - centerP * delayedR) / (1.0 + centerP);

        delayBufferL[writeIndex] = lpL - crossTalkL;
        delayBufferR[writeIndex] = lpR - crossTalkR;

        writeIndex++;
        if (writeIndex >= dn) writeIndex = 0;

        double rawOutL = delayedL + hpL;
        double rawOutR = delayedR + hpR;

        if (eqEnabled) {
            for (int b = 0; b < 3; ++b) {
                rawOutL = eqL[b].process(rawOutL);
                rawOutR = eqR[b].process(rawOutR);
            }
        }

        double outL = rawOutL * volume;
        double outR = rawOutR * volume;
        
        outL = std::clamp(outL, -1.0, 1.0);
        outR = std::clamp(outR, -1.0, 1.0);

        spectrumBuffer[spectrumIndex] = (outL + outR) * 0.5;
        spectrumIndex = (spectrumIndex + 1) % 1024;

        // Finale Konvertierung für ALSA Hardware Puffer
        outAlsaBuffer32[i * 2]     = static_cast<int32_t>(outL * 2147483647.0);
        outAlsaBuffer32[i * 2 + 1] = static_cast<int32_t>(outR * 2147483647.0);
    }
}

// -------------------------------------------------------------------------
// ANALYZER / WEB GUI LOGIK
// -------------------------------------------------------------------------
std::vector<double> RACEDspEngine::getSpectrumBands() {
    std::vector<double> audioData(1024, 0.0);
    
    {
        std::lock_guard<std::mutex> lock(dspMutex);
        for(int i = 0; i < 1024; i++) {
            audioData[i] = spectrumBuffer[(spectrumIndex + i) % 1024];
        }
    } 

    std::vector<std::complex<double>> complexData(1024);
    for(int i = 0; i < 1024; i++) {
        double multiplier = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / 1023.0));
        complexData[i] = std::complex<double>(audioData[i] * multiplier, 0.0);
    }

    computeFFT(complexData);

    std::vector<double> bands(64, 0.0);
    double minFreq = 20.0;
    double maxFreq = 22050.0; // Für Audio-Visualisierung auf 22kHz belassen
    double logMin = std::log10(minFreq);
    double logMax = std::log10(maxFreq);

    for (int i = 1; i < 512; i++) {
        // FFT Bins basierend auf der tatsächlichen Sample Rate berechnen
        double freq = (double)i * sampleRate / 1024.0; 
        
        if (freq < minFreq) continue;
        if (freq > maxFreq) break;
        
        int bandIndex = (int)((std::log10(freq) - logMin) / (logMax - logMin) * 64.0);
        if (bandIndex >= 0 && bandIndex < 64) {
            double mag = std::abs(complexData[i]);
            if (mag > bands[bandIndex]) bands[bandIndex] = mag;
        }
    }

    for(int i = 0; i < 64; i++) {
        double db = 20.0 * std::log10(bands[i] + 1e-6);
        double normalized = (db + 70.0) / 70.0; // -70dB bis 0dB Skala
        
        normalized = std::clamp(normalized, 0.0, 1.0);
        bands[i] = normalized;
    }

    return bands;
}
