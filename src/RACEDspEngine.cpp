#include "RACEDspEngine.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------
// FFT Helper für den Analyzer
// ---------------------------------------------------------
void computeFFT(std::vector<std::complex<float>>& data) {
    size_t n = data.size();
    if (n <= 1) return;
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t j = 0; j < len / 2; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ---------------------------------------------------------
// BiquadFilter (PEQ)
// ---------------------------------------------------------
BiquadFilter::BiquadFilter() : b0(0), b1(0), b2(0), a1(0), a2(0), z1(0), z2(0), freq(1000.0), q(0.707), gainDb(0.0), sampleRate(192000) {}

void BiquadFilter::setParameters(double frequency, double qFactor, double gain, int sr) {
    freq = frequency; q = qFactor; gainDb = gain; sampleRate = sr;
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / static_cast<double>(sampleRate);
    double alpha = std::sin(w0) / (2.0 * q);
    
    double a0 = 1.0 + alpha / A;
    b0 = static_cast<float>((1.0 + alpha * A) / a0);
    b1 = static_cast<float>((-2.0 * std::cos(w0)) / a0);
    b2 = static_cast<float>((1.0 - alpha * A) / a0);
    a1 = static_cast<float>((-2.0 * std::cos(w0)) / a0);
    a2 = static_cast<float>((1.0 - alpha / A) / a0);
}

float BiquadFilter::process(float in) {
    float out = in * b0 + z1;
    z1 = in * b1 - out * a1 + z2;
    z2 = in * b2 - out * a2;
    return out;
}

// ---------------------------------------------------------
// RaceFilter
// ---------------------------------------------------------
RaceFilter::RaceFilter(FilterType t) : type(t), b0(0), b1(0), b2(0), a1(0), a2(0), z1_1(0), z2_1(0), z1_2(0), z2_2(0) {
    update(400.0, 192000);
}

void RaceFilter::update(double cutoffFreq, int sr) {
    double w0 = 2.0 * M_PI * cutoffFreq / static_cast<double>(sr);
    double alpha = std::sin(w0) / (2.0 * 0.70710678); 
    double a0 = 1.0 + alpha;

    if (type == Lowpass) {
        b0 = static_cast<float>(((1.0 - std::cos(w0)) / 2.0) / a0);
        b1 = static_cast<float>((1.0 - std::cos(w0)) / a0);
        b2 = static_cast<float>(((1.0 - std::cos(w0)) / 2.0) / a0);
    } else { 
        b0 = static_cast<float>(((1.0 + std::cos(w0)) / 2.0) / a0);
        b1 = static_cast<float>(-(1.0 + std::cos(w0)) / a0);
        b2 = static_cast<float>(((1.0 + std::cos(w0)) / 2.0) / a0);
    }
    
    a1 = static_cast<float>((-2.0 * std::cos(w0)) / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
}

float RaceFilter::process(float in) {
    float out1 = in * b0 + z1_1;
    z1_1 = in * b1 - out1 * a1 + z2_1;
    z2_1 = in * b2 - out1 * a2;
    
    float out2 = out1 * b0 + z1_2;
    z1_2 = out1 * b1 - out2 * a1 + z2_2;
    z2_2 = out1 * b2 - out2 * a2;
    
    return out2;
}

// ---------------------------------------------------------
// RACEDspEngine
// ---------------------------------------------------------
RACEDspEngine::RACEDspEngine(double initialDelayUs, double initialAttenuationDb, double initialCenterP, bool initialFreqLimit, int sr) 
    : sampleRate(sr), 
      raceLpL(RaceFilter::Lowpass), raceLpR(RaceFilter::Lowpass),
      raceHpL(RaceFilter::Highpass), raceHpR(RaceFilter::Highpass) 
{
    spectrumBuffer.resize(32768, 0.0f);
    spectrumIndex = 0;

    int maxDelaySamples = sr; 
    delayBufferL.resize(maxDelaySamples, 0.0f);
    delayBufferR.resize(maxDelaySamples, 0.0f);
    writeIndex = 0;

    volume = 1.0;
    raceEnabled = true;
    filtersEnabled = true;
    eqEnabled = false;

    currentCrossoverFreq = 400.0;
    raceLpL.update(currentCrossoverFreq, sampleRate);
    raceLpR.update(currentCrossoverFreq, sampleRate);
    raceHpL.update(currentCrossoverFreq, sampleRate);
    raceHpR.update(currentCrossoverFreq, sampleRate);
    
    for (int i = 0; i < 3; ++i) {
        double f = (i == 0) ? 100.0 : (i == 1) ? 1000.0 : 5000.0;
        eqL[i].setParameters(f, 0.707, 0.0, sampleRate);
        eqR[i].setParameters(f, 0.707, 0.0, sampleRate);
    }

    setParameters(initialDelayUs, initialAttenuationDb, initialCenterP, initialFreqLimit);
}

RACEDspEngine::~RACEDspEngine() {}

void RACEDspEngine::setParameters(double newDelayUs, double newAttenuationDb, double newCenterP, bool newFreqLimit) {
    std::lock_guard<std::mutex> lock(dspMutex);
    delayUs = newDelayUs;
    attenuationDb = newAttenuationDb;
    attenuationLinear = std::pow(10.0, attenuationDb / 20.0);
    centerP = newCenterP;
    freqLimitRACE = newFreqLimit;
    
    dn = static_cast<int>(delayUs * static_cast<double>(sampleRate) / 1000000.0);
    if (dn < 1) dn = 1;
}

void RACEDspEngine::setVolume(double newVolume) { std::lock_guard<std::mutex> lock(dspMutex); volume = newVolume; }
void RACEDspEngine::setRaceEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); raceEnabled = enabled; }
void RACEDspEngine::setFiltersEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); filtersEnabled = enabled; }
void RACEDspEngine::setEqEnabled(bool enabled) { std::lock_guard<std::mutex> lock(dspMutex); eqEnabled = enabled; }

void RACEDspEngine::setEqBand(int bandIndex, double freq, double q, double gainDb) {
    std::lock_guard<std::mutex> lock(dspMutex);
    if(bandIndex >= 0 && bandIndex < 3) { 
        eqL[bandIndex].setParameters(freq, q, gainDb, sampleRate); 
        eqR[bandIndex].setParameters(freq, q, gainDb, sampleRate); 
    }
}

// ---------------------------------------------------------
// NEU: Direkte float* Puffer Verarbeitung (ALSA Loopback)
// ---------------------------------------------------------
void RACEDspEngine::processSamples(float* interleavedSamples, size_t frames) {
    std::lock_guard<std::mutex> lock(dspMutex);
    
    int bufferSize = delayBufferL.size();

    for (size_t i = 0; i < frames; i++) {
        float currentL = interleavedSamples[i * 2];
        float currentR = interleavedSamples[i * 2 + 1];

        if (!raceEnabled) {
            float outL = currentL * volume;
            float outR = currentR * volume;
            interleavedSamples[i * 2] = outL;
            interleavedSamples[i * 2 + 1] = outR;
            
            spectrumBuffer[spectrumIndex] = (outL + outR) * 0.5f;
            spectrumIndex = (spectrumIndex + 1) % 32768;
            continue;
        }

        float lpL = currentL, lpR = currentR;
        float hpL = 0.0f, hpR = 0.0f;

        if (filtersEnabled && freqLimitRACE) {
            hpL = raceHpL.process(currentL); 
            hpR = raceHpR.process(currentR);
            lpL = raceLpL.process(currentL); 
            lpR = raceLpR.process(currentR);
        }

        int readIndex = (writeIndex - dn + bufferSize) % bufferSize;
        float delayedL = delayBufferL[readIndex];
        float delayedR = delayBufferR[readIndex];

        float crossTalkL = static_cast<float>(attenuationLinear * (delayedR - centerP * delayedL) / (1.0 + centerP));
        float crossTalkR = static_cast<float>(attenuationLinear * (delayedL - centerP * delayedR) / (1.0 + centerP));

        delayBufferL[writeIndex] = lpL - crossTalkL;
        delayBufferR[writeIndex] = lpR - crossTalkR;
        writeIndex = (writeIndex + 1) % bufferSize;

        float outL = delayedL + hpL;
        float outR = delayedR + hpR;

        if (eqEnabled) {
            for (int b = 0; b < 3; ++b) {
                outL = eqL[b].process(outL); 
                outR = eqR[b].process(outR);
            }
        }

        outL *= volume;
        outR *= volume;

        // Modernes, sauberes Clipping mit C++17
        outL = std::clamp(outL, -1.0f, 1.0f);
        outR = std::clamp(outR, -1.0f, 1.0f);

        spectrumBuffer[spectrumIndex] = (outL + outR) * 0.5f;
        spectrumIndex = (spectrumIndex + 1) % 32768;

        interleavedSamples[i * 2] = outL;
        interleavedSamples[i * 2 + 1] = outR;
    }
}

// ---------------------------------------------------------
// Analyzer Funktionen
// ---------------------------------------------------------
std::vector<double> RACEDspEngine::getSpectrumBands() {
    std::vector<float> audioData(32768, 0.0f);
    
    {
        std::lock_guard<std::mutex> lock(dspMutex);
        for(int i = 0; i < 32768; i++) {
            audioData[i] = spectrumBuffer[(spectrumIndex + i) % 32768];
        }
    }
    
    std::vector<std::complex<float>> complexData(32768);
    for(int i = 0; i < 32768; i++) {
        float multiplier = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / 32767.0f));
        complexData[i] = std::complex<float>(audioData[i] * multiplier, 0.0f);
    }
    
    computeFFT(complexData);
    
    std::vector<double> bands(32, 0.0);
    float minFreq = 20.0f, maxFreq = 20000.0f;
    float logMin = std::log10(minFreq), logMax = std::log10(maxFreq);
    
    for (int b = 0; b < 32; b++) {
        float freqStart = std::pow(10.0f, logMin + (float)b / 32.0f * (logMax - logMin));
        float freqEnd   = std::pow(10.0f, logMin + (float)(b + 1) / 32.0f * (logMax - logMin));
        
        int binStart = (int)(freqStart * 32768.0f / static_cast<float>(sampleRate));
        int binEnd   = (int)(freqEnd * 32768.0f / static_cast<float>(sampleRate));
        
        if (binStart < 1) binStart = 1; 
        if (binEnd > 4095) binEnd = 4095; 
        if (binStart > binEnd) binEnd = binStart;
        
        float maxMag = 0.0f;
        for (int i = binStart; i <= binEnd; i++) {
            float mag = std::abs(complexData[i]) / 16384.0f;
            if (mag > maxMag) maxMag = mag;
        }
        
        float db = 20.0f * std::log10(maxMag + 1e-6f);
        float normalized = (db + 70.0f) / 70.0f; 
        if (normalized < 0.0f) normalized = 0.0f; 
        if (normalized > 1.0f) normalized = 1.0f;
        
        bands[b] = static_cast<double>(normalized);
    }
    
    return bands;
}

// ---------------------------------------------------------
// Getter-Implementierungen
// ---------------------------------------------------------
double RACEDspEngine::getDelayUs() { return delayUs; }
double RACEDspEngine::getAttenuationDb() { return attenuationDb; }
double RACEDspEngine::getCenterP() { return centerP; }
bool RACEDspEngine::getFreqLimit() { return freqLimitRACE; }
double RACEDspEngine::getVolume() { return volume; }
bool RACEDspEngine::getRaceEnabled() { return raceEnabled; }
bool RACEDspEngine::getFiltersEnabled() { return filtersEnabled; }
bool RACEDspEngine::getEqEnabled() { return eqEnabled; }

double RACEDspEngine::getEqFreq(int bandIndex) {
    if (bandIndex >= 0 && bandIndex < 3) return eqL[bandIndex].getFreq();
    return 1000.0;
}

double RACEDspEngine::getEqQ(int bandIndex) {
    if (bandIndex >= 0 && bandIndex < 3) return eqL[bandIndex].getQ();
    return 0.707;
}

double RACEDspEngine::getEqGain(int bandIndex) {
    if (bandIndex >= 0 && bandIndex < 3) return eqL[bandIndex].getGain();
    return 0.0;
}

void RACEDspEngine::setCrossoverFreq(double freq) {
    std::lock_guard<std::mutex> lock(dspMutex);
    currentCrossoverFreq = freq;
    raceLpL.update(freq, sampleRate);
    raceLpR.update(freq, sampleRate);
    raceHpL.update(freq, sampleRate);
    raceHpR.update(freq, sampleRate);
}

double RACEDspEngine::getCrossoverFreq() {
    return currentCrossoverFreq;
}
