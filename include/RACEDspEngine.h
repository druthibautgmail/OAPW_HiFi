#ifndef RACE_DSP_ENGINE_H
#define RACE_DSP_ENGINE_H

#include <vector>
#include <mutex>
#include <array>
#include <complex>
#include <cstdint>

// ---------------------------------------------------------
// BiquadFilter: Jetzt auch mit nativem LowPass und HighPass
// nach den Audio EQ Cookbook Formeln (RBJ)
// ---------------------------------------------------------
class BiquadFilter {
private:
    double b0, b1, b2, a1, a2;
    double z1, z2; 
    double freq, q, gainDb;
    int sampleRate;

public:
    BiquadFilter();
    
    // Peaking EQ
    void setParameters(double frequency, double qFactor, double gainDb, int sampleRate = 192000);
    
    // NEU: LowPass & HighPass für die RACE-Begrenzung
    void setLowPass(double frequency, double qFactor, int sampleRate = 192000);
    void setHighPass(double frequency, double qFactor, int sampleRate = 192000);
    
    double process(double in);
    
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

    std::vector<double> spectrumBuffer;
    int spectrumIndex;

    std::vector<double> delayBufferL;
    std::vector<double> delayBufferR;
    int writeIndex;

    // NEU: Kaskadierte Biquads (2x 2nd Order = 4th Order Linkwitz-Riley/Butterworth)
    // Ersetzt die alten instabilen 19-Tap IIR Filter
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

    std::vector<double> getSpectrumBands();

    void processSamples(std::vector<double>& interleavedSamples);
    void processFramesForAlsa(const double* inBuffer64, int32_t* outAlsaBuffer32, int frames);
};

#endif // RACE_DSP_ENGINE_H
