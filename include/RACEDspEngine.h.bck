// Neue Version, die auf double precision verzichtet, um die Performance zu steigern
// und rhythmische Aussetzer des Audiostreams zu vermeiden
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
// ---------------------------------------------------------
class BiquadFilter {
private:
    float b0, b1, b2, a1, a2; // Geändert von double auf float
    float z1, z2;             // Geändert von double auf float
    double freq, q, gainDb;   // Parameter bleiben double für präzise GUI-Einstellung
    int sampleRate;

public:
    BiquadFilter();
    
    // Peaking EQ
    void setParameters(double frequency, double qFactor, double gainDb, int sampleRate = 192000);
    
    // LowPass & HighPass für die RACE-Begrenzung
    void setLowPass(double frequency, double qFactor, int sampleRate = 192000);
    void setHighPass(double frequency, double qFactor, int sampleRate = 192000);
    
    float process(float in); // Geändert: Nimmt und liefert float für maximale Schleifen-Performance
    
    double getFreq() const { return freq; }
    double getQ() const { return q; }
    double getGain() const { return gainDb; }
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

    // Analyzer-Puffer: Muss float sein, da auch die schnelle FFT jetzt mit float rechnet
    std::vector<float> spectrumBuffer; // Geändert von double auf float
    int spectrumIndex;

    // Delay-Puffer: Extrem kritisch für den RAM-Durchsatz, jetzt hocheffizient als float
    std::vector<float> delayBufferL; // Geändert von double auf float
    std::vector<float> delayBufferR; // Geändert von double auf float
    int writeIndex;

    // Kaskadierte Biquads (Nutzen jetzt die optimierte float-Verarbeitung)
    std::array<BiquadFilter, 2> lpfL;
    std::array<BiquadFilter, 2> lpfR;
    std::array<BiquadFilter, 2> hpfL;
    std::array<BiquadFilter, 2> hpfR;

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

    // Liefert dem Web-GUI weiterhin double-Werte für die Anzeige, 
    // die interne Berechnung geschieht aber blockierungsfrei auf floats.
    std::vector<double> getSpectrumBands();

    // Die beiden Kern-Render-Funktionen
    void processSamples(std::vector<double>& interleavedSamples);
    void processFramesForAlsa(const double* inBuffer64, int32_t* outAlsaBuffer32, int frames);
};

#endif // RACE_DSP_ENGINE_H
