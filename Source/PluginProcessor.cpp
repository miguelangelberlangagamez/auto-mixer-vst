#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// HARD TUNER v4
//==============================================================================
void HardTuner::prepare (double sampleRate)
{
    sr = sampleRate;

    fftbuf.assign ((size_t) MAXN * 2, 0.0f);
    cbi.assign (MAXN, 0.0f);
    cbf.assign (MAXN, 0.0f);
    cbo.assign (MAXN, 0.0f);
    frag.assign (MAXN, 0.0f);
    cbwin.assign (MAXN, 0.0f);
    hannwin.assign (MAXN, 0.0f);
    acwinv.assign (MAXN, 0.0f);
    cbiwr = 0; cbord = 0;

    rebuildForMode();

    inpitch = outpitch = 0.0f;
    conf = 0.0f;
    phasein = phaseout = 0.0;
    inphinc = outphinc = 440.0 / sr;
    phincfact = 1.0;
    fragsize = 0;

    // corrector de formantes (celosia adaptativa, orden 7)
    falph = std::pow (0.001f, 80.0f / (float) sr);
    flamb = -(0.8517f * std::sqrt (std::atan (0.06583f * (float) sr)) - 0.1916f);
    flpa  = std::pow (0.001f, 10.0f / (float) sr);
    fmutealph = std::pow (0.001f, 1.0f / (float) sr);
    frlambC = flamb;                                   // fwarp = 0
    fmute = 1.0f;
    fhp = flp = 0.0f;
    for (int o = 0; o < ford; ++o)
    {
        fk[o] = fb_[o] = fc_[o] = frb[o] = frc[o] = fsig[o] = fsmooth[o] = ftvec[o] = 0.0f;
        fbuff[o].assign (MAXN, 0.0f);
    }

    std::fill (std::begin (pcHist), std::end (pcHist), 0.0f);
    analyzing = false;
    samplesLeft = 0;
    finishedFlag = false;
}


void HardTuner::rebuildForMode()
{
    Ncur  = liveMode ? 1024 : 2048;      // LIVE: ~21 ms (suelo 94 Hz) | ESTUDIO: ~43 ms (70 Hz)
    NfCur = Ncur / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (liveMode ? 10 : 11);

    nmin = (long) (sr / 700.0);
    nmax = (long) (sr / 70.0);
    if (nmax > NfCur - 1) nmax = NfCur - 1;

    std::fill (cbwin.begin(), cbwin.end(), 0.0f);
    for (int k = 0; k < Ncur / 2; ++k)
        cbwin[(size_t) (k + Ncur / 4)] = -0.5f * std::cos (4.0f * juce::MathConstants<float>::pi * (float) k / (float) (Ncur - 1)) + 0.5f;

    std::fill (hannwin.begin(), hannwin.end(), 0.0f);
    for (int k = 0; k < Ncur; ++k)
        hannwin[(size_t) k] = -0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) k / (float) Ncur) + 0.5f;

    std::fill (fftbuf.begin(), fftbuf.end(), 0.0f);
    for (int k = 0; k < Ncur; ++k) fftbuf[(size_t) k] = cbwin[(size_t) k];
    fft->performRealOnlyForwardTransform (fftbuf.data());
    for (int i = 0; i <= Ncur / 2; ++i)
    {
        const float re = fftbuf[(size_t) (2 * i)], im = fftbuf[(size_t) (2 * i + 1)];
        fftbuf[(size_t) (2 * i)] = re * re + im * im;
        fftbuf[(size_t) (2 * i + 1)] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (fftbuf.data());
    {
        const float w0 = juce::jmax (fftbuf[0], 1.0e-12f);
        std::fill (acwinv.begin(), acwinv.end(), 0.0f);
        acwinv[0] = 1.0f;
        for (int k = 1; k < Ncur; ++k)
        {
            const float r = fftbuf[(size_t) k] / w0;
            acwinv[(size_t) k] = (r > 1.0e-6f) ? 1.0f / r : 0.0f;
        }
    }

    std::fill (cbi.begin(), cbi.end(), 0.0f);
    std::fill (cbf.begin(), cbf.end(), 0.0f);
    std::fill (cbo.begin(), cbo.end(), 0.0f);
    std::fill (frag.begin(), frag.end(), 0.0f);
    for (int o = 0; o < ford; ++o)
        std::fill (fbuff[o].begin(), fbuff[o].end(), 0.0f);
    cbiwr = 0; cbord = 0;
    phasein = phaseout = 0.0;
    fragsize = 0;
}

void HardTuner::setLiveMode (bool live)
{
    if (live == liveMode) return;
    liveMode = live;
    rebuildForMode();
}

void HardTuner::runLowRate (float amount, int keyIx, int scaleIx)
{
    // mascara de escala absoluta (pc 0..11): 1 = nota permitida
    int notes[12];
    if (scaleIx == 2)
        for (int pc = 0; pc < 12; ++pc) notes[pc] = 1;
    else
    {
        static const bool minorSet[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
        static const bool majorSet[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
        const bool* set = (scaleIx == 1) ? majorSet : minorSet;
        for (int pc = 0; pc < 12; ++pc)
        {
            int rel = (pc - keyIx) % 12; if (rel < 0) rel += 12;
            notes[pc] = set[rel] ? 1 : -1;
        }
    }
    // La rejilla de Autotalent indexa pc 0 = LA. Convertimos: at[i] = nota (A+i)
    int atNotes[12];
    for (int i = 0; i < 12; ++i) atNotes[i] = notes[(i + 9) % 12];

    int pitch2note[12], note2pitch[12], numNotes = 0;
    for (int i = 0; i < 12; ++i)
    {
        if (atNotes[i] >= 0) { pitch2note[i] = numNotes; note2pitch[numNotes] = i; ++numNotes; }
        else                   pitch2note[i] = -1;
    }
    for (int i = numNotes; i < 12; ++i) note2pitch[i] = -1;

    // ---- autocovarianza de la ventana actual ----
    std::fill (fftbuf.begin(), fftbuf.end(), 0.0f);
    const int wr = cbiwr;
    for (int k = 0; k < Ncur; ++k)
        fftbuf[(size_t) k] = cbi[(size_t) ((wr - k + Ncur) % Ncur)] * cbwin[(size_t) k];
    fft->performRealOnlyForwardTransform (fftbuf.data());
    fftbuf[0] = 0.0f; fftbuf[1] = 0.0f;                    // quitar DC
    for (int i = 1; i <= Ncur / 2; ++i)
    {
        const float re = fftbuf[(size_t) (2 * i)], im = fftbuf[(size_t) (2 * i + 1)];
        fftbuf[(size_t) (2 * i)] = re * re + im * im;
        fftbuf[(size_t) (2 * i + 1)] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (fftbuf.data());
    float* ac = fftbuf.data();
    const float ac0 = juce::jmax (ac[0], 1.0e-12f);
    for (int k = 1; k < Ncur; ++k) ac[k] /= ac0;
    ac[0] = 1.0f;

    // ---- pico sesgado + confianza des-sesgada + centro de masa ----
    float best = 0.0f; long bi = 0;
    for (long k = nmin; k < nmax; ++k)
    {
        const float v = ac[k];
        if (v > ac[k - 1] && v >= ac[k + 1] && v > best) { best = v; bi = k; }
    }
    float pperiod = (float) nmax / (float) sr;
    if (best > 0.0f)
    {
        conf = best * acwinv[(size_t) bi];
        const float a = ac[bi - 1], b = ac[bi], c2 = ac[bi + 1];
        const float den = a + b + c2;
        const float com = (den > 1.0e-12f)
                        ? (a * (float) (bi - 1) + b * (float) bi + c2 * (float) (bi + 1)) / den
                        : (float) bi;
        pperiod = com / (float) sr;
    }
    else conf = 0.0f;

    // semitonos relativos a A4 (aref); solo se actualiza con VOZ; si no, SE CONGELA
    const float st = -12.0f * std::log2 (aref * pperiod);
    const bool voiced = (conf >= 0.7f);
    if (voiced) inpitch = st;

    if (analyzing.load() && voiced)
    {
        int pc = ((int) std::lround (69.0f + inpitch)) % 12;
        if (pc < 0) pc += 12;
        accumulateKey (pc);
    }
    if (analyzing.load())
    {
        const int left = samplesLeft.fetch_sub (Ncur / noverlap) - Ncur / noverlap;
        if (left <= 0) finishAnalysis();
    }

    // ---- correccion SIN ESTADOS (segmento de seno entre notas vecinas) ----
    outpitch = inpitch;
    {
        int   oct = (int) (outpitch / 12.0f + 32.0f) - 32;
        float sem = outpitch - 12.0f * (float) oct;
        int lo = (int) sem, hi = lo + 1;
        int lowersnap, uppersnap;
        if (atNotes[((lo % 12) + 12) % 12] < 0 || atNotes[((hi % 12) + 12) % 12] < 0)
            lowersnap = uppersnap = 1;
        else
        {
            lowersnap = (atNotes[((lo % 12) + 12) % 12] == 1);
            uppersnap = (atNotes[((hi % 12) + 12) % 12] == 1);
        }
        while (atNotes[((lo % 12) + 12) % 12] < 0) --lo;
        while (atNotes[((hi % 12) + 12) % 12] < 0) ++hi;
        float noteix = (sem - (float) lo) / (float) (hi - lo) + (float) pitch2note[((lo % 12) + 12) % 12];
        if (lo < 0) noteix -= (float) numNotes;
        float op = noteix + (float) numNotes * (float) oct;   // pitch en espacio de notas

        const int   base = (int) (op + 128.0f) - 128;
        const float fr   = op - (float) base - 0.5f;
        const int   gapS = hi - lo;
        float scaleF = (gapS > 2) ? (float) gapS * 0.5f : 1.0f;
        const float smooth = juce::jmax (0.001f, retuneSmooth);   // 0 = retune duro
        float t = juce::jlimit (-0.5f, 0.5f, fr * scaleF / smooth);
        float corrected = 0.5f * std::sin (juce::MathConstants<float>::pi * t) + 0.5f + (float) base;
        if ((fr < 0.5f && lowersnap) || (fr >= 0.5f && uppersnap))
            op = amount * corrected + (1.0f - amount) * op;

        // volver de espacio de notas a semitonos
        int   oct2 = (int) (op / (float) numNotes + 32.0f) - 32;
        float rem  = op - (float) numNotes * (float) oct2;
        int lo2 = (int) rem, hi2 = lo2 + 1;
        float span = (float) (note2pitch[hi2 % numNotes] - note2pitch[lo2]);
        if (hi2 >= numNotes) span += 12.0f;
        float semis = span * (rem - (float) lo2) + (float) note2pitch[lo2];
        outpitch = semis + 12.0f * (float) oct2;
        outpitch = juce::jlimit (-36.0f, 24.0f, outpitch);
    }

    // fases del shifter
    inphinc   = (double) aref * std::pow (2.0, inpitch  / 12.0) / sr;
    outphinc  = (double) aref * std::pow (2.0, outpitch / 12.0) / sr;
    phincfact = outphinc / inphinc;
}

void HardTuner::process (juce::AudioBuffer<float>& buffer,
                         float amount, int keyIx, int scaleIx)
{
    const int n  = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    const bool active = amount >= 0.005f || analyzing.load();

    for (int i = 0; i < n; ++i)
    {
        const float x = (R != nullptr) ? 0.5f * (L[i] + R[i]) : L[i];
        float t = x;

        // -------- corrector de formantes: PRE (aplana con celosia adaptativa)
        float fa = t - fhp;   // pre-enfasis paso-alto
        fhp = t;
        float fb2 = fa;
        const float foma = 1.0f - falph;
        for (int o = 0; o < ford; ++o)
        {
            fsig[o] = fa * fa * foma + fsig[o] * falph;
            const float fcv = (fb2 - fc_[o]) * flamb + fb_[o];
            fc_[o] = fcv; fb_[o] = fb2;
            fk[o] = fa * fcv * foma + fk[o] * falph;
            float tf = fk[o] / (fsig[o] + 1.0e-6f);
            tf = tf * foma + fsmooth[o] * falph;
            fsmooth[o] = tf;
            fbuff[o][(size_t) cbiwr] = tf;
            fb2 = fcv - tf * fa;
            fa  = fa  - tf * fcv;
        }
        cbi[(size_t) cbiwr] = x;
        cbf[(size_t) cbiwr] = fa;

        ++cbiwr; if (cbiwr >= Ncur) cbiwr = 0;

        // -------- seccion de baja tasa: deteccion + correccion cada Ncur/4
        if (active && (cbiwr % (Ncur / noverlap)) == 0)
            runLowRate (amount, keyIx, scaleIx);

        // -------- shifter por acumuladores de fase (fase continua, sin epocas)
        phasein  += inphinc;
        phaseout += outphinc;

        if (phasein >= 1.0)      // un fragmento por PERIODO DE ENTRADA
        {
            phasein -= 1.0;
            const int c2 = cbiwr - Ncur / 2;
            for (int k = -Ncur / 2; k < Ncur / 2; ++k)
                frag[(size_t) ((k + Ncur) % Ncur)] = cbf[(size_t) ((k + c2 + Ncur) % Ncur)];
        }

        if (phaseout >= 1.0)     // un grano por PERIODO DE SALIDA
        {
            fragsize *= 2;
            if (fragsize > Ncur) fragsize = Ncur;
            phaseout -= 1.0;
            const int c2 = cbord + Ncur / 2;
            long gl = (long) ((double) fragsize / phincfact);
            if (gl >= Ncur / 2) gl = Ncur / 2 - 1;
            if (gl > 2)
            {
                for (long k = -gl / 2; k < gl / 2; ++k)
                {
                    const float w = hannwin[(size_t) (Ncur / 2 + k * Ncur / gl)];
                    const double indd = phincfact * (double) k;
                    const int i1 = (int) std::floor (indd);
                    const int i0 = i1 - 1, i2 = i1 + 1, i3 = i1 + 2;
                    const float d0 = (float) (indd - i0), d1 = (float) (indd - i1),
                                d2 = (float) (indd - i2), d3 = (float) (indd - i3);
                    const float v0 = frag[(size_t) ((i0 % Ncur + Ncur) % Ncur)];
                    const float v1 = frag[(size_t) ((i1 % Ncur + Ncur) % Ncur)];
                    const float v2 = frag[(size_t) ((i2 % Ncur + Ncur) % Ncur)];
                    const float v3 = frag[(size_t) ((i3 % Ncur + Ncur) % Ncur)];
                    float v = -0.166666666667f * v0 * d1 * d2 * d3
                            +  0.5f            * v1 * d0 * d2 * d3
                            -  0.5f            * v2 * d0 * d1 * d3
                            +  0.166666666667f * v3 * d0 * d1 * d2;
                    const size_t oi = (size_t) (((k + c2) % Ncur + Ncur) % Ncur);
                    cbo[oi] += v * w;
                }
            }
            fragsize = 0;
        }
        ++fragsize;

        t = cbo[(size_t) cbord];
        cbo[(size_t) cbord] = 0.0f;
        ++cbord; if (cbord >= Ncur) cbord = 0;

        // -------- corrector de formantes: POST (los re-aplica; identidad si no hay shift)
        const int rd = (cbiwr + 2) % Ncur;
        {
            const float tin = t;
            float f0resp, f1resp;
            // pasada 1: respuesta a 0
            fa = 0.0f; fb2 = fa;
            for (int o = 0; o < ford; ++o)
            {
                const float fcv = (fb2 - frc[o]) * frlambC + frb[o];
                const float tf  = fbuff[o][(size_t) rd];
                fb2 = fcv - tf * fa;
                ftvec[o] = tf * fcv;
                fa -= ftvec[o];
            }
            float acc = -fa;
            for (int o = ford - 1; o >= 0; --o) acc += ftvec[o];
            f0resp = acc;
            // pasada 2: respuesta a 1
            fa = 1.0f; fb2 = fa;
            for (int o = 0; o < ford; ++o)
            {
                const float fcv = (fb2 - frc[o]) * frlambC + frb[o];
                const float tf  = fbuff[o][(size_t) rd];
                fb2 = fcv - tf * fa;
                ftvec[o] = tf * fcv;
                fa -= ftvec[o];
            }
            acc = -fa;
            for (int o = ford - 1; o >= 0; --o) acc += ftvec[o];
            f1resp = acc;
            // resolver el lazo sin retardo
            float num = 2.0f * tin;
            const float den = 1.0f - f1resp + f0resp;
            float y = (den != 0.0f) ? (num + f0resp) / den : 0.0f;
            // pasada 3: actualizar registros
            fa = y; fb2 = fa;
            for (int o = 0; o < ford; ++o)
            {
                const float fcv = (fb2 - frc[o]) * frlambC + frb[o];
                frc[o] = fcv; frb[o] = fb2;
                const float tf = fbuff[o][(size_t) rd];
                fb2 = fcv - tf * fa;
                fa  = fa  - tf * fcv;
            }
            y += flpa * flp;    // post-enfasis paso-bajo
            flp = y;
            if (fmute > 0.5f) y *= (fmute - 0.5f) * 2.0f;
            else              y  = 0.0f;
            fmute = (1.0f - fmutealph) + fmutealph * fmute;
            t = y;
        }

        const float out = active ? t : cbi[(size_t) rd];
        L[i] = out;
        if (R != nullptr) R[i] = out;
    }
}

void HardTuner::startKeyAnalysis()
{
    std::fill (std::begin (pcHist), std::end (pcHist), 0.0f);
    samplesLeft.store ((int) (15.0 * sr));
    finishedFlag = false;
    analyzing = true;
}

float HardTuner::analysisSecondsLeft() const
{
    return (float) samplesLeft.load() / (float) sr;
}

void HardTuner::accumulateKey (int pitchClass)
{
    pcHist[pitchClass] += 1.0f;
}

void HardTuner::finishAnalysis()
{
    static const float majorP[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                      2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minorP[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                      2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float bestScore = -1.0f;
    int bestKey = 0, bestScale = 0;

    for (int root = 0; root < 12; ++root)
    {
        float sMaj = 0.0f, sMin = 0.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float h = pcHist[(pc + root) % 12];
            sMaj += h * majorP[pc];
            sMin += h * minorP[pc];
        }
        if (sMaj > bestScore) { bestScore = sMaj; bestKey = root; bestScale = 1; }
        if (sMin > bestScore) { bestScore = sMin; bestKey = root; bestScale = 0; }
    }
    resultKey.store (bestKey);
    resultScale.store (bestScale);
    analyzing = false;
    finishedFlag = true;
}

juce::String HardTuner::getResultText() const
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (names[resultKey.load() % 12])
         + (resultScale.load() == 1 ? " Mayor" : " Menor");
}

float HardTuner::snapToScale (float midiNote, int keyIx, int scaleIx)
{
    static const bool minorSet[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
    static const bool majorSet[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };

    const int nearest = (int) std::lround (midiNote);

    if (scaleIx == 2)
        return (float) nearest;

    const bool* set = (scaleIx == 1) ? majorSet : minorSet;

    for (int off = 0; off <= 6; ++off)
    {
        for (const int cand : { nearest + off, nearest - off })
        {
            int pc = (cand - keyIx) % 12;
            if (pc < 0) pc += 12;
            if (set[pc])
                return (float) cand;
        }
    }
    return (float) nearest;
}

UndergroundVoxProcessor::UndergroundVoxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pGate   = apvts.getRawParameterValue ("gate");
    pLive   = apvts.getRawParameterValue ("live");
    pTuneOn = apvts.getRawParameterValue ("tuneOn");
    pFxOn   = apvts.getRawParameterValue ("fxOn");
    pTune   = apvts.getRawParameterValue ("tune");
    pClean  = apvts.getRawParameterValue ("clean");
    pPunch  = apvts.getRawParameterValue ("punch");
    pDeess  = apvts.getRawParameterValue ("deess");
    pAir    = apvts.getRawParameterValue ("air");
    pDrive  = apvts.getRawParameterValue ("drive");
    pEcho   = apvts.getRawParameterValue ("echo");
    pSpace  = apvts.getRawParameterValue ("space");
    pDuck   = apvts.getRawParameterValue ("duck");
    pOutput = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout UndergroundVoxProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto pct = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);

    params.push_back (std::make_unique<P> ("gate",   "Gate",   pct, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("live", "Live Mode (baja latencia)", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("tuneOn", "Auto-Tune ON", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("fxOn",   "FX ON",        true));
    params.push_back (std::make_unique<P> ("tune",   "Tune",   pct, 0.9f));
    params.push_back (std::make_unique<P> ("clean",  "EQ",     pct, 0.5f));
    params.push_back (std::make_unique<P> ("punch",  "Punch",  pct, 0.5f));
    params.push_back (std::make_unique<P> ("deess",  "De-Ess", pct, 0.5f));
    params.push_back (std::make_unique<P> ("air",    "Air",    pct, 0.4f));
    params.push_back (std::make_unique<P> ("drive",  "Color",  pct, 0.3f));
    params.push_back (std::make_unique<P> ("echo",   "Echo",   pct, 0.15f));
    params.push_back (std::make_unique<P> ("space",  "Space",  pct, 0.2f));
    params.push_back (std::make_unique<P> ("duck",   "Duck",   pct, 0.5f));
    params.push_back (std::make_unique<P> ("output", "Output",
                        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "scale", "Scale",
        juce::StringArray { "Menor", "Mayor", "Cromatica" }, 0));

    return { params.begin(), params.end() };
}

//==============================================================================
void UndergroundVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) getTotalNumOutputChannels() };

    gate.prepare (spec);
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gate.setRelease (120.0f);

    tuner.prepare (sampleRate);
    setLatencySamples (tuner.getLatencySamples());

    lowCut.prepare (spec);
    bell500.prepare (spec);
    bell1570.prepare (spec);
    bodyBell.prepare (spec);
    finalHP1.prepare (spec);
    finalHP2.prepare (spec);

    compVocal.prepare (spec);
    compVocal.setAttack (0.016f);
    compVocal.setRelease (2000.0f);

    compPeaks.prepare (spec);
    compPeaks.setAttack (1.0f);
    compPeaks.setRelease (100.0f);

    makeupGain.prepare (spec);
    makeupGain.setRampDurationSeconds (0.05);

    split7Low.prepare (spec);   split7Low.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
    split7High.prepare (spec);  split7High.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    split14Low.prepare (spec);  split14Low.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    split14High.prepare (spec); split14High.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    split7Low.setCutoffFrequency   (7000.0f);
    split7High.setCutoffFrequency  (7000.0f);
    split14Low.setCutoffFrequency  (14000.0f);
    split14High.setCutoffFrequency (14000.0f);

    deEsserComp.prepare (spec);
    deEsserComp.setAttack (0.15f);
    deEsserComp.setRelease (60.0f);
    deEsserComp.setRatio (12.0f);

    airMid.prepare (spec);
    airHigh.prepare (spec);

    delayL.prepare (spec);
    delayR.prepare (spec);
    delayL.reset();
    delayR.reset();
    reverb.prepare (spec);

    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);

    safetyLimiter.prepare (spec);
    safetyLimiter.setThreshold (-1.0f);
    safetyLimiter.setRelease (60.0f);

    const int ch = (int) spec.numChannels;
    highBuf.setSize (ch, samplesPerBlock);
    topBuf.setSize (ch, samplesPerBlock);
    midDryBuf.setSize (ch, samplesPerBlock);
    colorBuffer.setSize (ch, samplesPerBlock);
    reverbBuf.setSize (ch, samplesPerBlock);
    delayWetBuf.setSize (ch, samplesPerBlock);

    duckAttackCoef  = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    duckReleaseCoef = 1.0f - std::exp (-1.0f / (0.250f * (float) sampleRate));
    duckEnv = 0.0f;

    updateParameters();
}

bool UndergroundVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
void UndergroundVoxProcessor::updateParameters()
{
    const float gateAmt = pGate->load();
    const float clean   = pClean->load();
    const float punch   = pPunch->load();
    const float deess   = pDeess->load();
    const float air     = pAir->load();
    const float space   = pSpace->load();

    gate.setThreshold (-70.0f + gateAmt * 40.0f);

    const float lcFreq = 80.0f + clean * 115.0f;
    const float bellDb = clean * -3.5f;

    *lowCut.state   = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        currentSampleRate, lcFreq, 0.4f);
    *bell500.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate, 500.0f, 1.0f, juce::Decibels::decibelsToGain (bellDb));
    *bell1570.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate, 1570.0f, 1.0f, juce::Decibels::decibelsToGain (bellDb));

    const float thV = -6.0f - punch * 17.0f;
    const float raV = 2.0f + punch * 2.0f;
    compVocal.setThreshold (thV);
    compVocal.setRatio (raV);
    compPeaks.setThreshold (-4.0f - punch * 6.0f);
    compPeaks.setRatio (6.0f);

    const float grV = juce::jmax (0.0f, (-10.0f - thV)) * (1.0f - 1.0f / raV);
    makeupGain.setGainDecibels (grV * 0.65f);

    deEsserComp.setThreshold (-12.0f - deess * 26.0f);

    // AIR domado: menos brillo estridente a la misma posicion de perilla
    *airMid.state  = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        currentSampleRate, 4500.0f, 0.5f, juce::Decibels::decibelsToGain (air * 3.2f));
    *airHigh.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        currentSampleRate, 12500.0f, 0.5f, juce::Decibels::decibelsToGain (air * 7.0f));

    // CUERPO calido: campana fija suave en los medios-graves de la voz
    *bodyBell.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate, 230.0f, 0.9f, juce::Decibels::decibelsToGain (2.0f));

    // corte final de sub-graves: 80 Hz, Butterworth de 4o orden (24 dB/oct)
    *finalHP1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        currentSampleRate, 80.0f, 0.5412f);
    *finalHP2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        currentSampleRate, 80.0f, 1.3066f);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize   = 0.82f;
    rp.damping    = 0.45f;
    rp.width      = 1.0f;
    rp.wetLevel   = space * 0.5f;
    rp.dryLevel   = 0.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    outputGain.setGainDecibels (pOutput->load() + (air * -1.5f));
    const bool wantLive = pLive != nullptr && pLive->load() > 0.5f;
    if (wantLive != tuner.getLiveMode())
        tuner.setLiveMode (wantLive);

    const bool tuneOn = pTuneOn == nullptr || pTuneOn->load() > 0.5f;
    const int wantLat = tuneOn ? tuner.getLatencySamples() : 0;
    if (wantLat != getLatencySamples())
        setLatencySamples (wantLat);
}

void UndergroundVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    updateParameters();

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    const bool tuneOn = pTuneOn == nullptr || pTuneOn->load() > 0.5f;
    const bool fxOn   = pFxOn   == nullptr || pFxOn->load()   > 0.5f;

    // 1) GATE
    if (fxOn)
        gate.process (ctx);

    // 2) TUNE (apagable para usar un afinador externo; latencia -> 0)
    if (tuneOn)
    {
        const int keyIx   = (int) apvts.getRawParameterValue ("key")->load();
        const int scaleIx = (int) apvts.getRawParameterValue ("scale")->load();
        tuner.process (buffer, pTune->load(), keyIx, scaleIx);
    }

    if (fxOn)
    {
    // 3) EQ
    lowCut.process (ctx);
    bell500.process (ctx);
    bell1570.process (ctx);
    bodyBell.process (ctx);

    // 4) PUNCH
    compVocal.process (ctx);
    compPeaks.process (ctx);
    makeupGain.process (ctx);

    // 5) DE-ESS
    {
        highBuf.makeCopyOf (buffer, true);
        juce::dsp::AudioBlock<float> highBlock (highBuf);
        juce::dsp::ProcessContextReplacing<float> highCtx (highBlock);

        split7Low.process (ctx);
        split7High.process (highCtx);

        topBuf.makeCopyOf (highBuf, true);
        juce::dsp::AudioBlock<float> topBlock (topBuf);
        juce::dsp::ProcessContextReplacing<float> topCtx (topBlock);

        split14Low.process (highCtx);
        split14High.process (topCtx);

        midDryBuf.makeCopyOf (highBuf, true);
        deEsserComp.process (highCtx);

        const float rangeFloor = juce::Decibels::decibelsToGain (-14.67f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* mid       = highBuf.getWritePointer (ch);
            const float* dry = midDryBuf.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                mid[i] = mid[i] * (1.0f - rangeFloor) + dry[i] * rangeFloor;

            buffer.addFrom (ch, 0, highBuf, ch, 0, numSamples);
            buffer.addFrom (ch, 0, topBuf,  ch, 0, numSamples);
        }
    }

    // 6a) AIR
    airMid.process (ctx);
    airHigh.process (ctx);

    // 6b) COLOR
    {
        const float color = pDrive->load();
        if (color > 0.001f)
        {
            colorBuffer.makeCopyOf (buffer, true);

            const float driveIn = 1.0f + color * 2.5f;
            const float bias    = 0.15f;
            const float comp    = 1.0f / std::tanh ((1.0f + bias) * driveIn * 0.6f);
            const float wet     = 0.25f + color * 0.45f;
            const float dry     = 1.0f - wet * 0.8f;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                const float* c = colorBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = c[i] * driveIn * 0.6f;
                    const float saturated = (std::tanh (x + bias) - std::tanh (bias)) * comp;
                    d[i] = d[i] * dry + saturated * wet;
                }
            }
        }
    }

    // 7) ECHO + SPACE con DUCK
    {
        const float echo  = pEcho->load();
        const float space = pSpace->load();
        const float duck  = pDuck->load();

        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto hostBpm = pos->getBpm())
                    bpm = *hostBpm;

        const float delaySamples = (float) (currentSampleRate * (60.0 / bpm) * 0.75);
        delayL.setDelay (juce::jlimit (1.0f, 191000.0f, delaySamples));
        delayR.setDelay (juce::jlimit (1.0f, 191000.0f, delaySamples * 1.5f));

        const float dWet = echo * 0.5f;
        const float fb   = 0.25f + echo * 0.25f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float* l = buffer.getReadPointer (0);
            const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

            const float outL = delayL.popSample (0);
            const float outR = delayR.popSample (0);

            delayL.pushSample (0, l[i] + outL * fb);
            delayR.pushSample (0, r[i] + outR * fb);

            delayWetBuf.setSample (0, i, outL * dWet);
            if (numCh > 1) delayWetBuf.setSample (1, i, outR * dWet);
        }

        if (space > 0.001f)
        {
            reverbBuf.makeCopyOf (buffer, true);
            juce::dsp::AudioBlock<float> rvBlock (reverbBuf);
            juce::dsp::ProcessContextReplacing<float> rvCtx (rvBlock);
            reverb.process (rvCtx);
        }
        else
            reverbBuf.clear();

        for (int i = 0; i < numSamples; ++i)
        {
            float voiceAbs = std::abs (buffer.getSample (0, i));
            if (numCh > 1)
                voiceAbs = juce::jmax (voiceAbs, std::abs (buffer.getSample (1, i)));

            const float coef = voiceAbs > duckEnv ? duckAttackCoef : duckReleaseCoef;
            duckEnv += (voiceAbs - duckEnv) * coef;

            const float amount   = juce::jmin (1.0f, duckEnv * 12.0f);
            const float duckDb   = -duck * 16.0f * amount;
            const float duckGain = juce::Decibels::decibelsToGain (duckDb);

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                d[i] += (delayWetBuf.getSample (ch, i) + reverbBuf.getSample (ch, i)) * duckGain;
            }
        }
    }

    // 8) OUTPUT + LIMITER
    } // fxOn
    outputGain.process (ctx);
    if (fxOn)
    {
        finalHP1.process (ctx);   // corte 80 Hz: fuera todo el sub
        finalHP2.process (ctx);
    }
    safetyLimiter.process (ctx);
}

//==============================================================================
void UndergroundVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void UndergroundVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* UndergroundVoxProcessor::createEditor()
{
    return new UndergroundVoxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UndergroundVoxProcessor();
}
