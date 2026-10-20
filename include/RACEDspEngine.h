// Neue Version, optimiert für native float* Puffer Verarbeitung (ALSA Loopback)
// Dynamisches Linkwitz-Riley 4. Ordnung Filter-Design integriert
// U. Thibaut, 10.10.2026 / Aktualisiert für float* API
#ifndef RACE_DSP_ENGINE_H
#define RACE_DSP_ENGINE_H

#include <vector>
#include <mutex>
#include <array>
#include <complex>
#include <cstdint>

// ---------------------------------------------------------
// BiquadFilter: Intern auf float umgestellt für RPi 5 NEON SIMD
// ---------------------------------------------------------
class BiquadFilter {
private:
    float b0, b1, b2, a1, a2; 
    float z1, z2;             
    double freq, q, gainDb;   
    int sampleRate;

public:
    BiquadFilter();
    void setParameters(double frequency, double qFactor, double gainDb, int sampleRate = 192000);
    float process(float in); 
    double getFreq() const { return freq; }
    double getQ() const { return q; }
    double getGain() const { return gainDb; }
};

// ---------------------------------------------------------
// RaceFilter: Dynamischer RACE Frequenzweichen-Filter 
// ---------------------------------------------------------
class RaceFilter {
public:
    enum FilterType { Lowpass, Highpass };
    RaceFilter(FilterType type);
    void update(double cutoffFreq, int sampleRate);
    float process(float in);
    
private:
    FilterType type;
    float b0, b1, b2, a1, a2;
    float z1_1, z2_1; 
    float z1_2, z2_2; 
};

// ---------------------------------------------------------
// RACEDspEngine
// ---------------------------------------------------------
class RACEDspEngine {
private:
    int sampleRate;

    double delayUs;            
    double attenuationDb;      
    double attenuationLinear; 
    int dn;
    
    double centerP;
    bool freqLimitRACE;
    
    double currentCrossoverFreq; 
    
    double volume; 
    bool raceEnabled;
    bool filtersEnabled;
    bool eqEnabled; 

    // Analyzer-Puffer
    std::vector<float> spectrumBuffer; 
    int spectrumIndex;

    // Delay-Puffer
    std::vector<float> delayBufferL; 
    std::vector<float> delayBufferR; 
    int writeIndex;

    RaceFilter raceLpL;
    RaceFilter raceLpR;
    RaceFilter raceHpL;
    RaceFilter raceHpR;

    std::array<BiquadFilter, 3> eqL;
    std::array<BiquadFilter, 3> eqR;

    std::mutex dspMutex;

public:
    RACEDspEngine(double initialDelayUs, double initialAttenuationDb, double initialCenterP, bool initialFreqLimit, int sampleRate = 192000);
    ~RACEDspEngine();

    void setParameters(double newDelayUs, double newAttenuationDb, double newCenterP, bool newFreqLimit);
    
    void setCrossoverFreq(double freq);
    double getCrossoverFreq();
    
    void setVolume(double newVolume); 
    void setRaceEnabled(bool enabled);
    void setFiltersEnabled(bool enabled);
    
    void setEqEnabled(bool enabled);
    void setEqBand(int bandIndex, double freq, double q, double gainDb);

    double getDelayUs();
    double getAttenuationDb();
    double getCenterP();
    bool getFreqLimit();
    double getVolume();
    bool getRaceEnabled();
    bool getFiltersEnabled();
    
    bool getEqEnabled();
    double getEqFreq(int bandIndex);
    double getEqQ(int bandIndex);
    double getEqGain(int bandIndex);

    std::vector<double> getSpectrumBands();

    // NEU: Nimmt direkt den ALSA Float-Buffer und arbeitet "in-place"
    void processSamples(float* interleavedSamples, size_t frames);
};

#endif // RACE_DSP_ENGINE_H
