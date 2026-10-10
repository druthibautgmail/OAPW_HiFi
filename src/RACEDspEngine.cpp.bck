#include "RACEDspEngine.h"
#include <iostream>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Schneller FFT Algorithmus (Auf float umgestellt für SIMD-Speed auf RPi5) ---
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
        float angle = -2.0f * static_cast<float>(M_PI) / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
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

// --- BiquadFilter (float-basiert für NEON SIMD) ---
BiquadFilter::BiquadFilter() : b0(0.0f), b1(0.0f), b2(0.0f), a1(0.0f), a2(0.0f), z1(0.0f), z2(0.0f), freq(1000.0), q(0.707), gainDb(0.0), sampleRate(192000) {
    setParameters(1000.0, 0.707, 0.0, 192000); 
}

void BiquadFilter::setParameters(double frequency, double qFactor, double gain, int sr) {
    freq = frequency; q = qFactor; gainDb = gain; sampleRate = sr;
    
    float A = std::pow(10.0f, static_cast<float>(gainDb) / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * static_cast<float>(freq) / sampleRate;
    float alpha = std::sin(w0) / (2.0f * static_cast<float>(q));
    
    float a0 = 1.0f + alpha / A;
    b0 = (1.0f + alpha * A) / a0;
    b1 = (-2.0f * std::cos(w0)) / a0;
    b2 = (1.0f - alpha * A) / a0;
    a1 = (-2.0f * std::cos(w0)) / a0;
    a2 = (1.0f - alpha / A) / a0;
}

void BiquadFilter::setLowPass(double frequency, double qFactor, int sr) {
    freq = frequency; q = qFactor; sampleRate = sr;
    float w0 = 2.0f * static_cast<float>(M_PI) * static_cast<float>(freq) / sampleRate;
    float alpha = std::sin(w0) / (2.0f * static_cast<float>(q));
    
    float a0 = 1.0f + alpha;
    b0 = ((1.0f - std::cos(w0)) / 2.0f) / a0;
    b1 = (1.0f - std::cos(w0)) / a0;
    b2 = ((1.0f - std::cos(w0)) / 2.0f) / a0;
    a1 = (-2.0f * std::cos(w0)) / a0;
    a2 = (1.0f - alpha) / a0;
}

void BiquadFilter::setHighPass(double frequency, double qFactor, int sr) {
    freq = frequency; q = qFactor; sampleRate = sr;
    float w0 = 2.0f * static_cast<float>(M_PI) * static_cast<float>(freq) / sampleRate;
    float alpha = std::sin(w0) / (2.0f * static_cast<float>(q));
    
    float a0 = 1.0f + alpha;
    b0 = ((1.0f + std::cos(w0)) / 2.0f) / a0;
    b1 = -(1.0f + std::cos(w0)) / a0;
    b2 = ((1.0f + std::cos(w0)) / 2.0f) / a0;
    a1 = (-2.0f * std::cos(w0)) / a0;
    a2 = (1.0f - alpha) / a0;
}

float BiquadFilter::process(float in) {
    float out = in * b0 + z1;
    z1 = in * b1 - out * a1 + z2;
    z2 = in * b2 - out * a2;
    return out;
}

// --- RACEDspEngine Konstruktor ---
RACEDspEngine::RACEDspEngine(double initialDelayUs, double initialAttenuationDb, double initialCenterP, bool initialFreqLimit, int sr) 
    : sampleRate(sr) {
    
    volume = 1.0; 
    raceEnabled = true;
    filtersEnabled = true;
    eqEnabled = false;

    double raceHpFreq = 150.0;  
    double raceLpFreq = 4000.0; 
    
    for (int i = 0; i < 2; ++i) {
        lpfL[i].setLowPass(raceLpFreq, 0.707, sampleRate);
        lpfR[i].setLowPass(raceLpFreq, 0.707, sampleRate);
        hpfL[i].setHighPass(raceHpFreq, 0.707, sampleRate);
        hpfR[i].setHighPass(raceHpFreq, 0.707, sampleRate);
    }

    for (int i = 0; i < 3; ++i) {
        double f = (i == 0) ? 100.0 : (i == 1) ? 1000.0 : 5000.0;
        eqL[i].setParameters(f, 0.707, 0.0, sampleRate);
        eqR[i].setParameters(f, 0.707, 0.0, sampleRate);
    }

    spectrumBuffer.resize(1024, 0.0f);
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

    int calculatedDn = static_cast<int>(std::round(delayUs * sampleRate / 1000000.0));
    dn = (calculatedDn > 0) ? calculatedDn : 1;
    attenuationLinear = std::pow(10.0, attenuationDb / 20.0);

    delayBufferL.assign(dn, 0.0f);
    delayBufferR.assign(dn, 0.0f);
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

double RACEDspEngine::getDelayUs() { return delayUs; }
double RACEDspEngine::getAttenuationDb() { return attenuationDb; }
double RACEDspEngine::getCenterP() { return centerP; }
bool RACEDspEngine::getFreqLimit() { return freqLimitRACE; }
double RACEDspEngine::getVolume() { return volume; }
bool RACEDspEngine::getRaceEnabled() { return raceEnabled; }
bool RACEDspEngine::getFiltersEnabled() { return filtersEnabled; }
bool RACEDspEngine::getEqEnabled() { return eqEnabled; }

double RACEDspEngine::getEqFreq(int b) { return (b>=0 && b<3) ? eqL[b].getFreq() : 0.0; }
double RACEDspEngine::getEqQ(int b) { return (b>=0 && b<3) ? eqL[b].getQ() : 0.0; }
double RACEDspEngine::getEqGain(int b) { return (b>=0 && b<3) ? eqL[b].getGain() : 0.0; }

// --- REIN OPTIMIERTE ALSA ROUTINE (KEIN MUTEX - KEINE BLOCKADE) ---
void RACEDspEngine::processFramesForAlsa(const double* inBuffer64, int32_t* outAlsaBuffer32, int frames) {
    float vol = static_cast<float>(volume);
    bool raceOn = raceEnabled;
    bool fltOn = filtersEnabled;
    bool lmtOn = freqLimitRACE;
    bool eqOn = eqEnabled;
    float attLin = static_cast<float>(attenuationLinear);
    float cP = static_cast<float>(centerP);
    int currentDn = dn;

    for (int i = 0; i < frames; ++i) {
        float currentL = static_cast<float>(inBuffer64[i * 2]);
        float currentR = static_cast<float>(inBuffer64[i * 2 + 1]);

        if (!raceOn) {
            float bypassL = currentL * vol;
            float bypassR = currentR * vol;
            
            int idx = spectrumIndex;
            spectrumBuffer[idx] = (bypassL + bypassR) * 0.5f;
            spectrumIndex = (idx + 1) % 1024;

            bypassL = std::clamp(bypassL, -1.0f, 1.0f);
            bypassR = std::clamp(bypassR, -1.0f, 1.0f);
            outAlsaBuffer32[i * 2]     = static_cast<int32_t>(bypassL * 2147483647.0f);
            outAlsaBuffer32[i * 2 + 1] = static_cast<int32_t>(bypassR * 2147483647.0f);
            continue;
        }

        float lpL = currentL, lpR = currentR, hpL = currentL, hpR = currentR;

        if (fltOn && lmtOn) {
            for (int k = 0; k < 2; ++k) {
                hpL = hpfL[k].process(hpL);
                hpR = hpfR[k].process(hpR);
                lpL = lpfL[k].process(lpL);
                lpR = lpfR[k].process(lpR);
            }
        }

        float delayedL = delayBufferL[writeIndex];
        float delayedR = delayBufferR[writeIndex];

        float crossTalkL = attLin * (delayedR - cP * delayedL) / (1.0f + cP);
        float crossTalkR = attLin * (delayedL - cP * delayedR) / (1.0f + cP);

        delayBufferL[writeIndex] = lpL - crossTalkL;
        delayBufferR[writeIndex] = lpR - crossTalkR;

        writeIndex++;
        if (writeIndex >= currentDn) writeIndex = 0;

        float rawOutL = delayedL + hpL;
        float rawOutR = delayedR + hpR;

        if (eqOn) {
            for (int b = 0; b < 3; ++b) {
                rawOutL = eqL[b].process(rawOutL);
                rawOutR = eqR[b].process(rawOutR);
            }
        }

        float outL = rawOutL * vol;
        float outR = rawOutR * vol;
        
        outL = std::clamp(outL, -1.0f, 1.0f);
        outR = std::clamp(outR, -1.0f, 1.0f);

        int idx = spectrumIndex;
        spectrumBuffer[idx] = (outL + outR) * 0.5f;
        spectrumIndex = (idx + 1) % 1024;

        outAlsaBuffer32[i * 2]     = static_cast<int32_t>(outL * 2147483647.0f);
        outAlsaBuffer32[i * 2 + 1] = static_cast<int32_t>(outR * 2147483647.0f);
    }
}

// --- WEB GUI ANALYZER ---
std::vector<double> RACEDspEngine::getSpectrumBands() {
    std::vector<float> audioData(1024, 0.0f);
    
    int currentIdx = spectrumIndex;
    for(int i = 0; i < 1024; i++) {
        audioData[i] = spectrumBuffer[(currentIdx + i) % 1024];
    }

    std::vector<std::complex<float>> complexData(1024);
    for(int i = 0; i < 1024; i++) {
        float multiplier = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / 1023.0f));
        complexData[i] = std::complex<float>(audioData[i] * multiplier, 0.0f);
    }

    computeFFT(complexData);

    std::vector<double> bands(64, 0.0);
    double minFreq = 20.0;
    double maxFreq = 22050.0; 
    double logMin = std::log10(minFreq);
    double logMax = std::log10(maxFreq);

    for (int i = 1; i < 512; i++) {
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
	double normalized = (db + 70.0) / 70.0;
	normalized = std::clamp(normalized, 0.0, 1.0);
	bands[i] = normalized;
    }
return bands;
}
