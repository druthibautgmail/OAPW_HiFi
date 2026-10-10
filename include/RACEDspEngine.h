// Neue Version, die auf double precision verzichtet, um die Performance zu steigern
// und rhythmische Aussetzer des Audiostreams zu vermeiden
// Dynamisches Linkwitz-Riley 4. Ordnung Filter-Design integriert
// U. Thibaut, 10.10.2026
#ifndef RACE_DSP_ENGINE_H
#define RACE_DSP_ENGINE_H

#include <vector>
#include <mutex>
#include <array>
#include <complex>
#include <cstdint>

// ---------------------------------------------------------
// BiquadFilter: Intern auf float umgestellt für RPi 5 NEON SIMD
// (Wird jetzt ausschließlich für die PEQ-Bänder verwendet)
// ---------------------------------------------------------
class BiquadFilter {
private:
    float b0, b1, b2, a1, a2; 
    float z1, z2;             
    double freq, q, gainDb;   
    int sampleRate;

public:
    BiquadFilter();
    
    // Peaking EQ
    void setParameters(double frequency, double qFactor, double gainDb, int sampleRate = 192000);
    
    float process(float in); 
    
    double getFreq() const { return freq; }
    double getQ() const { return q; }
    double getGain() const { return gainDb; }
};

// ---------------------------------------------------------
// RaceFilter: Dynamischer RACE Frequenzweichen-Filter 
// (Linkwitz-Riley 4. Ordnung, intern float-optimiert)
// ---------------------------------------------------------
class RaceFilter {
public:
    enum FilterType { Lowpass, Highpass };
    RaceFilter(FilterType type);
    
    // Berechnet die Koeffizienten dynamisch zur Laufzeit neu
    void update(double cutoffFreq, int sampleRate);
    float process(float in);
    
private:
    FilterType type;
    float b0, b1, b2, a1, a2;
    
    // State-Variablen für zwei kaskadierte Stufen = 4. Ordnung
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

    // Dynamische RACE-Filter (ersetzen die alten IIR/Biquad-Arrays)
    RaceFilter raceLpL;
    RaceFilter raceLpR;
    RaceFilter raceHpL;
    RaceFilter raceHpR;

    // EQ Bänder
    std::array<BiquadFilter, 3> eqL;
    std::array<BiquadFilter, 3> eqR;

    std::mutex dspMutex;

public:
    RACEDspEngine(double initialDelayUs, double initialAttenuationDb, double initialCenterP, bool initialFreqLimit, int sampleRate = 192000);
    ~RACEDspEngine();

    void setParameters(double newDelayUs, double newAttenuationDb, double newCenterP, bool newFreqLimit);
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

    // Die beiden Kern-Render-Funktionen
    void processSamples(std::vector<double>& interleavedSamples);
    void processFramesForAlsa(const double* inBuffer64, int32_t* outAlsaBuffer32, int frames);
};

#endif // RACE_DSP_ENGINE_H
