#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// HARD TUNER v4.4 - motor de CINTA (linea de retardo variable)
//
// Tras la gran caceria: los granos PSOLA quedan jubilados. Este motor:
//  1. CINTA: cuando no corrige es una copia exacta (transparencia por
//     construccion); al corregir, estira la lectura y empalma +-1 periodo
//     con microfundidos. Los empalmes se prohiben sobre transitorios.
//  2. COLA DE CONTROL ALINEADA: el ratio y la nota que se aplican en cada
//     instante corresponden al CONTENIDO que se esta leyendo (el hallazgo
//     que mato el bailoteo de silabas: control y audio viajan juntos).
//  3. DETECTOR ORACULO: autocorrelacion FFT sobre 43 ms + eleccion de
//     octava entre {mitad, actual, doble} con coste de continuidad
//     (domestica la fonacion fry de los ataques).
//  4. Decision de nota INSTANTANEA en silaba nueva (tras hueco >20 ms);
//     estabilidad anti-thrash solo dentro de la vocal.
//==============================================================================
class HardTuner
{
public:
    void prepare (double sampleRate);
    void setLiveMode (bool live);                  // ~21 ms para directo (suelo 94 Hz)
    bool getLiveMode() const { return liveMode; }

    void process (juce::AudioBuffer<float>& buffer,
                  float amount, int keyIx, int scaleIx);

    int getLatencySamples() const { return Ncur - 1; }

    // --- analisis de tonalidad (boton ANALYZE) ---
    void startKeyAnalysis();
    bool isAnalyzing() const        { return analyzing.load(); }
    float analysisSecondsLeft() const;
    bool consumeFinishedFlag()      { return finishedFlag.exchange (false); }
    int  getResultKey() const       { return resultKey.load(); }
    int  getResultScale() const     { return resultScale.load(); }
    juce::String getResultText() const;

private:
    void  runLowRate (float amount, int keyIx, int scaleIx);
    void  rebuildForMode();
    void  accumulateKey (int pitchClass);
    void  finishAnalysis();
    static float snapToScale (float midiNote, int keyIx, int scaleIx);

    double sr = 44100.0;

    // ================= NUCLEO AUTOTALENT (reimplementacion fiel) =============
    static constexpr int MAXN = 2048;              // asignacion maxima de buffers
    int Ncur = 2048;                               // ventana activa (2048 estudio / 1024 LIVE)
    int NfCur = 1025;
    bool liveMode = false;
    static constexpr int noverlap = 8;             // HYPER: deteccion cada N/8 (~5.3 ms)
    std::unique_ptr<juce::dsp::FFT> fft;           // orden 11
    std::vector<float> fftbuf;                     // 2*N (in-place JUCE)
    std::vector<float> cbi, cbf, cbo;              // entrada, aplanada, salida OLA
    std::vector<float> cbwin, hannwin, acwinv, frag;
    int  cbiwr = 0, cbord = 0;
    long nmin = 68, nmax = 685;
    float aref = 440.0f;
    float retuneSmooth = 0.0f;                     // 0 = retune duro (el efecto)

    float inpitch = 0.0f, outpitch = 0.0f, conf = 0.0f;
    double phasein = 0.0, phaseout = 0.0;
    double inphinc = 0.01, outphinc = 0.01, phincfact = 1.0;
    long  fragsize = 0;

    // corrector de formantes (celosia adaptativa)
    static constexpr int ford = 7;
    float fk[ford] = {}, fb_[ford] = {}, fc_[ford] = {},
          frb[ford] = {}, frc[ford] = {}, fsig[ford] = {},
          fsmooth[ford] = {}, ftvec[ford] = {};
    std::vector<float> fbuff[ford];
    float fhp = 0.0f, flp = 0.0f;
    float falph = 0.0f, flamb = 0.0f, frlambC = 0.0f, flpa = 0.0f;
    float fmute = 1.0f, fmutealph = 0.0f;

    // ---------- tonalidad bajo demanda ----------
    float pcHist[12] = {};
    std::atomic<bool> analyzing     { false };
    std::atomic<int>  samplesLeft   { 0 };
    std::atomic<bool> finishedFlag  { false };
    std::atomic<int>  resultKey     { 0 };
    std::atomic<int>  resultScale   { 0 };
};

//==============================================================================
// UNDERGROUND VOX v0.8
//==============================================================================
class UndergroundVoxProcessor : public juce::AudioProcessor
{
public:
    UndergroundVoxProcessor();
    ~UndergroundVoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Underground Vox"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    HardTuner& getTuner() { return tuner; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateParameters();

    double currentSampleRate = 44100.0;
    std::atomic<float>* pLive = nullptr;
    std::atomic<float>* pTuneOn = nullptr;
    std::atomic<float>* pFxOn = nullptr;

    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                        juce::dsp::IIR::Coefficients<float>>;

    juce::dsp::NoiseGate<float> gate;
    HardTuner tuner;
    StereoFilter lowCut, bell500, bell1570;
    StereoFilter bodyBell;                       // cuerpo calido +2 dB @ 230 Hz
    StereoFilter finalHP1, finalHP2;             // corte final 80 Hz, 24 dB/oct
    juce::dsp::Compressor<float> compVocal, compPeaks;
    juce::dsp::Gain<float> makeupGain;
    juce::dsp::LinkwitzRileyFilter<float> split7Low, split7High, split14Low, split14High;
    juce::dsp::Compressor<float> deEsserComp;
    juce::AudioBuffer<float> highBuf, topBuf, midDryBuf;
    StereoFilter airMid, airHigh;
    juce::AudioBuffer<float> colorBuffer;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 192000 },
                                                                                delayR { 192000 };
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> reverbBuf, delayWetBuf;
    float duckEnv = 0.0f;
    float duckAttackCoef = 0.0f, duckReleaseCoef = 0.0f;
    juce::dsp::Gain<float> outputGain;
    juce::dsp::Limiter<float> safetyLimiter;

    std::atomic<float>* pGate   = nullptr;
    std::atomic<float>* pTune   = nullptr;
    std::atomic<float>* pClean  = nullptr;
    std::atomic<float>* pPunch  = nullptr;
    std::atomic<float>* pDeess  = nullptr;
    std::atomic<float>* pAir    = nullptr;
    std::atomic<float>* pDrive  = nullptr;
    std::atomic<float>* pEcho   = nullptr;
    std::atomic<float>* pSpace  = nullptr;
    std::atomic<float>* pDuck   = nullptr;
    std::atomic<float>* pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndergroundVoxProcessor)
};
