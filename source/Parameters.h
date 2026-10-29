#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string_view>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
//
// PR7-K started afresh at 0.1.0: none of PR7-E's parameters carried over
// (its EQ, COMP, DOMINION and LIMITER code is kept for the effects rack, a
// later stage, which will give them new IDs of its own).
namespace ParamIDs
{
    // Global: not part of a preset.
    inline constexpr auto outputGain = "outputGain";   // the OUTPUT knob
    inline constexpr auto bypass     = "bypass";
    inline constexpr auto tune       = "tune";         // the whole synth, in cents (SETTINGS)

    // Saved with each preset: its own level, to balance presets against each other.
    inline constexpr auto patchLevel = "patchLevel";

    // The 8 oscillator tabs: "osc1On", "osc1Wave"... `what` is one of oscSettings.
    inline constexpr int numOscillators = 8;
    inline juce::String osc (int index, const char* what) { return "osc" + juce::String (index + 1) + what; }
    // Stage 2 (0.2.0) appended its unison, vibrato and chorder settings ("Ch1On"... for the chorder's 4 notes),
    // stage 3 (0.3.0) the WAVETABLE's: which factory table, INDEX, its mode and FRAMES,
    // 0.4.0 SHAPE's: SKEW, PINCH, FORMANT and its mode, CRUSH and its mode,
    // 0.5.0 SYNC (with its envelope), FM, AM and their modulator's RATE, GRID and PHASE LOCK,
    // 0.6.0 the FFT editor's SWEEP, FLIP, LP, HP, SLOPE, VSPEED and VSTRENGTH (its curve is kept with the preset),
    // 0.8.0 which SHAPERs the oscillator goes through ("Shp1" to "Shp4"),
    // 0.11.0 whether it follows the PITCH module's envelope.
    inline constexpr const char* oscSettings[] = {
        "On", "Wave", "Volume", "Pan", "Transpose", "Fine", "Random", "PhaseRand", "Phase", "Gain", "Width", "FadeIn", "Invert",
        "Noise", "NoiseColour", "NoiseWidth", "Sub", "SubWave", "SubOct", "SubInvert", "Filter", "Voices",
        "UniVoices", "UniMix", "UniDetune", "UniSpread", "UniOctaves",
        "VibMode", "VibWave", "VibRate", "VibAmount", "VibFade", "VibCurve",
        "Ch1On", "Ch1Coarse", "Ch1Fine", "Ch1Pan", "Ch1Vol", "Ch2On", "Ch2Coarse", "Ch2Fine", "Ch2Pan", "Ch2Vol",
        "Ch3On", "Ch3Coarse", "Ch3Fine", "Ch3Pan", "Ch3Vol", "Ch4On", "Ch4Coarse", "Ch4Fine", "Ch4Pan", "Ch4Vol",
        "Table", "WtIndex", "WtMode", "WtFrames",
        "Skew", "Pinch", "Formant", "FormantMode", "Crush", "CrushMode",
        "SyncAmount", "SyncMode", "SyncEnv", "SyncEnvTime", "SyncEnvCurve", "FmAmount", "FmSource", "AmAmount", "AmShape",
        "ModRate", "ModGrid", "ModLock",
        "FftSweep", "FftFlip", "FftLp", "FftHp", "FftSlope", "FftVSpeed", "FftVStrength",
        "Shp1", "Shp2", "Shp3", "Shp4",
        "PitchEnv",
        // Stage 4e: TRAVEL (its envelope, steps and strengths are kept with the preset, in the mod matrix).
        "TrOn", "TrEditors", "TrTrigger", "TrMode", "TrPatternMode", "TrSteps", "TrStrength", "TrVelocity", "TrVelCurve",
        "TrSmooth", "TrPhase", "TrFlip", "TrRate", "TrSync", "TrDivision",
        // Stage 5a: which ARP its notes come from.
        "Arp",
        // Stage 7b: how much it sends to DELAY and to REVERB.
        "SendDelay", "SendReverb",
        // Stage 7d: the insert bus it plays into (NONE, A to D).
        "Insert" };

    // Where a setting is in oscSettings, worked out by the compiler (a typo won't compile).
    consteval int oscSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (oscSettings); ++i)
            if (std::string_view (oscSettings[i]) == what)
                return i;
        throw "not an oscillator setting";
    }

    // The FILTER and AMP. Numbered from 1: Avenger has four of each, and
    // more may come in a later stage (an oscillator's Filter switch, now on or
    // off for filter 1, can become a choice of filters without breaking presets).
    inline constexpr auto fltOn       = "flt1On";
    inline constexpr auto fltType     = "flt1Type";
    inline constexpr auto fltCutoff   = "flt1Cutoff";     // Hz
    inline constexpr auto fltRes      = "flt1Res";        // 0 to 1
    inline constexpr auto fltDrive    = "flt1Drive";      // 0 to 1
    inline constexpr auto fltKey      = "flt1Key";        // keytrack, 0 to 1
    inline constexpr auto fltVel      = "flt1Vel";        // cutoff velocity, 0 to 1
    inline constexpr auto fltResVel   = "flt1ResVel";
    inline constexpr auto fltEnv      = "flt1Env";        // envelope amount, -1 to 1
    inline constexpr auto fltEnvVel   = "flt1EnvVel";
    inline constexpr auto fltAttack   = "flt1Attack";     // ms
    inline constexpr auto fltHold     = "flt1Hold";
    inline constexpr auto fltDecay    = "flt1Decay";
    inline constexpr auto fltSustain  = "flt1Sustain";    // 0 to 1
    inline constexpr auto fltRelease  = "flt1Release";
    inline constexpr auto fltAttackCurve  = "flt1AttackCurve";    // -1 to 1
    inline constexpr auto fltDecayCurve   = "flt1DecayCurve";
    inline constexpr auto fltReleaseCurve = "flt1ReleaseCurve";

    inline constexpr auto ampVolume   = "amp1Volume";     // 0 to 2
    inline constexpr auto ampVel      = "amp1Vel";        // 0 to 1
    inline constexpr auto ampPan      = "amp1Pan";        // -1 to 1
    inline constexpr auto ampPanKey   = "amp1PanKey";
    inline constexpr auto ampSpread   = "amp1Spread";
    inline constexpr auto ampSpike    = "amp1Spike";      // 0 to 1
    inline constexpr auto ampSpikeCurve = "amp1SpikeCurve";
    inline constexpr auto ampSpikeVel = "amp1SpikeVel";
    inline constexpr auto ampAttack   = "amp1Attack";
    inline constexpr auto ampHold     = "amp1Hold";
    inline constexpr auto ampDecay    = "amp1Decay";
    inline constexpr auto ampSustain  = "amp1Sustain";
    inline constexpr auto ampRelease  = "amp1Release";
    inline constexpr auto ampAttackCurve  = "amp1AttackCurve";
    inline constexpr auto ampDecayCurve   = "amp1DecayCurve";
    inline constexpr auto ampReleaseCurve = "amp1ReleaseCurve";

    // The SHAPERs (stage 3e, 0.8.0): four, numbered "shp1" to "shp4", each with these settings.
    inline constexpr int numShapers = 4;
    inline constexpr const char* shaperSettings[] = {
        "On", "Model", "Drive", "DriveVel", "Eq1Gain", "Eq1Freq", "Eq1Q", "Eq2Gain", "Eq2Freq", "Eq2Q", "EqSplit", "Place", "Mix", "Output" };
    inline juce::String shp (int index, const char* what) { return "shp" + juce::String (index + 1) + what; }
    consteval int shaperSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (shaperSettings); ++i)
            if (std::string_view (shaperSettings[i]) == what)
                return i;
        throw "not a shaper setting";
    }

    // MODULATION (stage 4a, 0.9.0): four LFOs ("lfo1" to "lfo4", each with
    // these settings), three macro knobs and two macro buttons. The mod
    // matrix itself isn't parameters: it's kept with the preset (ModMatrix).
    inline constexpr int numLfos = 4;
    inline constexpr const char* lfoSettings[] = {
        "Wave", "Rate", "Sync", "Division", "Trigger", "Phase", "Offset", "Delay", "FadeIn", "OneShot" };
    inline juce::String lfo (int index, const char* what) { return "lfo" + juce::String (index + 1) + what; }
    consteval int lfoSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (lfoSettings); ++i)
            if (std::string_view (lfoSettings[i]) == what)
                return i;
        throw "not an LFO setting";
    }
    // The modulation envelopes (stage 4b, 0.10.0): "menv1" to "menv8". Their points are kept with the preset (ModMatrix).
    inline constexpr int numEnvelopes = 8;
    inline constexpr const char* envelopeSettings[] = { "Mode", "Trigger", "Sync", "Speed", "Steps", "Flip", "Offset" };
    inline juce::String env (int index, const char* what) { return "menv" + juce::String (index + 1) + what; }
    consteval int envelopeSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (envelopeSettings); ++i)
            if (std::string_view (envelopeSettings[i]) == what)
                return i;
        throw "not an envelope setting";
    }
    // The ARPEGGIATORS (stage 5a, 0.15.0): "arp1" to "arp8". Their patterns are kept with the preset (ModMatrix).
    inline constexpr int numArps = 8;
    inline constexpr const char* arpSettings[] = { "On", "Mode", "Division", "Timing", "Octaves", "Gate", "Length", "Velocity",
                                                   "LowKey", "HighKey", "Humanize", "Strum", "Pattern", "Chain", "Trigger",
                                                   "NoteSource" };   // 5b (0.16.0): appended
    inline juce::String arp (int index, const char* what) { return "arp" + juce::String (index + 1) + what; }
    consteval int arpSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (arpSettings); ++i)
            if (std::string_view (arpSettings[i]) == what)
                return i;
        throw "not an arp setting";
    }
    inline constexpr const char* macros[] = { "macro1", "macro2", "macro3" };
    inline constexpr const char* macroButtons[] = { "macroButton1", "macroButton2" };

    // The PITCH module (stage 4c, 0.11.0). Its envelope's points are kept with the preset (ModMatrix).
    inline constexpr auto pitchEnvOn    = "pitchEnvOn";
    inline constexpr auto pitchEnvMode  = "pitchEnvMode";
    inline constexpr auto pitchEnvSpeed = "pitchEnvSpeed";
    inline constexpr auto pitchEnvRange = "pitchEnvRange";   // semitones, 0 to 24
    inline constexpr auto pitchEnvSteps = "pitchEnvSteps";
    inline constexpr auto pitchEnvFlip  = "pitchEnvFlip";
    inline constexpr auto pitchKeytrack = "pitchKeytrack";   // -1 to 1
    inline constexpr auto bendLag       = "bendLag";         // ms
    inline constexpr auto glideCurve    = "glideCurve";      // -1 to 1
    inline constexpr auto glideSplit    = "glideSplit";      // a note (0: off)

    // The STEP SEQUENCER (stage 4d, 0.12.0). Its steps are kept with the preset (ModMatrix).
    inline constexpr auto seqOn       = "seqOn";
    inline constexpr auto seqTrigger  = "seqTrigger";    // choice: Song, Voice, First Note, Last Note
    inline constexpr auto seqDivision = "seqDivision";   // SPEED: 8/1 to 1/64
    inline constexpr auto seqTiming   = "seqTiming";     // choice: Straight, Triplet, Dotted
    inline constexpr auto seqMode     = "seqMode";       // choice: Mono, Stereo
    inline constexpr auto seqLength   = "seqLength";     // 2 to 32 steps
    inline constexpr auto seqContour  = "seqContour";
    inline constexpr auto seqGate     = "seqGate";
    inline constexpr auto seqVolMix   = "seqVolMix";
    inline constexpr auto seqWidth    = "seqWidth";
    inline constexpr auto seqDecay    = "seqDecay";      // ms (0: off)
    inline constexpr auto seqFadeIn   = "seqFadeIn";     // ms
    inline constexpr auto seqDelay    = "seqDelay";      // ms
    inline constexpr auto seqLowKey   = "seqLowKey";     // the key zone
    inline constexpr auto seqHighKey  = "seqHighKey";
    inline constexpr auto shuffle     = "shuffle";       // the synth's SHUFFLE (the arpeggiator and drums will share it)

    // How the keyboard plays it.
    inline constexpr auto playMode  = "playMode";     // choice: Poly, Mono, Mono Low, Mono High
    inline constexpr auto legato    = "legato";
    inline constexpr auto glide     = "glide";        // ms
    inline constexpr auto glideMode = "glideMode";    // choice: Always, Legato
    inline constexpr auto bendUp    = "bendUp";       // semitones
    inline constexpr auto bendDown  = "bendDown";
    inline constexpr auto voices    = "voices";       // notes at once

    // ARPS TOGETHER (stage 5b, 0.16.0): every arp's steps counted from the first key held, so they stay in step.
    inline constexpr auto arpsTogether = "arpsTogether";

    // THE DRUMS (stage 6a, version hint 20): the kit's own settings, then each slot's, "drum1".."drum12" plus a setting.
    inline constexpr auto drumsOn       = "drumsOn";
    inline constexpr auto drumsRoot     = "drumsRoot";       // the kit's C: a MIDI note
    inline constexpr auto drumsLevel    = "drumsLevel";      // dB
    inline constexpr auto drumsVelocity = "drumsVelocity";   // 0 to 1
    inline constexpr int numDrums = 12;
    inline juce::String drum (int slot, const char* what) { return "drum" + juce::String (slot + 1) + what; }
    inline constexpr const char* drumSettings[] = { "Model", "Level", "Pan", "Pitch", "Decay", "Tone", "Snap", "Mute", "Solo",
                                                    "Choke", "Insert", "SendDelay", "SendReverb",
                                                    "SampleStart", "SampleEnd",     // 0.21.1 (version hint 21): appended
                                                    "Gain", "Mono", "Delay", "LowPass", "HighPass" };   // 6c (hint 23)
    consteval int drumSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (drumSettings); ++i)
            if (std::string_view (drumSettings[i]) == what)
                return i;
        throw "not a drum setting";
    }

    // THE DRUM SEQUENCER (stage 6b, version hint 22): the kit's beat. Its notes are preset data (ModMatrix::drumPatterns).
    inline constexpr auto dseqOn       = "dseqOn";
    inline constexpr auto dseqTrigger  = "dseqTrigger";    // choice: Song, Key (drumseq::triggerNames)
    inline constexpr auto dseqPattern  = "dseqPattern";    // choice: A-D (SONG plays it; KEY: the key held chooses)
    inline constexpr auto dseqDivision = "dseqDivision";   // SPEED: seq::divisionNames
    inline constexpr auto dseqTiming   = "dseqTiming";     // seq::timingNames
    inline constexpr auto dseqLength   = "dseqLength";     // beats, 1 to 16
    inline constexpr auto dseqVelocity = "dseqVelocity";   // VELOCITY AMOUNT: how much the key's velocity counts (KEY)

    // CONTROLLER TO MOD WHEEL (stage 4f, 0.14.0): these controllers drive the mod wheel too (the "mw" switches).
    inline constexpr auto mwBreath     = "mwBreath";
    inline constexpr auto mwExpression = "mwExpression";
    inline constexpr auto mwAftertouch = "mwAftertouch";
    inline constexpr auto mwSustain    = "mwSustain";

    // THE RACK (stage 7a, 0.17.0): PR7-E 0.18.1's four effects, with PR7-E's IDs (so their presets load in either
    // plugin) and its comments below (its "stomps" are our rack units; "rigs" our presets).
    // ECHELON, the EQ (inspired by FabFilter Pro-Q 4; see dsp/eq).
    inline constexpr auto eqOn        = "eqOn";
    inline constexpr auto eqOutput    = "eqOutput";
    inline constexpr auto eqGainScale = "eqGainScale";
    inline constexpr auto eqAutoGain  = "eqAutoGain";
    inline constexpr int eqNumBands = 24;
    // Each band's settings: "eqB1Used", "eqB1Freq"... `what` is one of the names below.
    // From 0.5.0, its dynamics: Dyn (the range), DynAuto, Thresh, Attack, Release.
    inline juce::String eqBand (int band, const char* what) { return "eqB" + juce::String (band + 1) + what; }
    inline constexpr const char* eqBandSettings[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place",
                                                      "Dyn", "DynAuto", "Thresh", "Attack", "Release" };

    // Stomp 2, COMP (0.9.0): PR7-C's compressor, ported. After the EQ.
    inline constexpr auto compOn        = "compOn";
    inline constexpr auto compThreshold = "compThreshold";   // dBFS
    inline constexpr auto compRatio     = "compRatio";       // 1 = SOFT, 30 = infinite
    inline constexpr auto compAttack    = "compAttack";      // ms
    inline constexpr auto compRelease   = "compRelease";     // ms
    inline constexpr auto compAuto      = "compAuto";        // AUTO release
    inline constexpr auto compMakeUp    = "compMakeUp";      // dB
    inline constexpr auto compCharacter = "compCharacter";   // choice: VCA, OPTO, FET
    inline constexpr auto compLink      = "compLink";        // both channels together
    // COMP's bands (0.13.0): multiband compression and expansion, inspired by
    // FabFilter Pro-MB (our own design, see dsp/effects/MultibandComp.h).
    // The bands come first, then the full-band compressor above, which
    // compWide switches (on by default, so older rigs sound as they did).
    inline constexpr auto compWide    = "compWide";
    inline constexpr auto mbLookahead = "mbLookahead";   // COMP 20 ms late, the bands hearing ahead
    inline constexpr auto mbMix       = "mbMix";         // %, 0 to 200
    inline constexpr int mbNumBands = 6;
    // "mbB1Used", "mbB1Low"... `what` is one of the names below.
    inline juce::String mbBand (int band, const char* what) { return "mbB" + juce::String (band + 1) + what; }
    inline constexpr const char* mbBandSettings[] = { "Used", "On", "Low", "High", "Slope", "Thresh", "Range", "Mode", "Ratio",
                                                      "Knee", "Attack", "Release", "Char", "Stereo", "Link", "Out", "Look" };

    // Stomp 5, DOMINION (0.10.0): dynamic resonance suppression, inspired by
    // oeksound soothe 3 (our own design, see dsp/dominion/Dominion.h). After
    // the EQ, before COMP.
    inline constexpr auto domOn      = "domOn";
    inline constexpr auto domMode    = "domMode";      // choice: Soft, Hard
    inline constexpr auto domDepth   = "domDepth";     // 0 to 20
    inline constexpr auto domDetail  = "domDetail";    // 0 to 100
    inline constexpr auto domAttack  = "domAttack";    // 0 to 100
    inline constexpr auto domRelease = "domRelease";   // 0 to 100
    inline constexpr auto domStereo  = "domStereo";    // choice: L/R, M/S
    inline constexpr auto domLink    = "domLink";      // %
    inline constexpr auto domFocus   = "domFocus";     // -100 to 100
    inline constexpr auto domDetailLow = "domDetailLow", domDetailHigh = "domDetailHigh",
                          domAttackLow = "domAttackLow", domAttackHigh = "domAttackHigh",
                          domReleaseLow = "domReleaseLow", domReleaseHigh = "domReleaseHigh";   // the tilts, -100 to 100
    inline constexpr auto domMaxCut  = "domMaxCut";    // dB (40: no limit)
    inline constexpr auto domWetTrim = "domWetTrim";   // dB
    inline constexpr auto domMix     = "domMix";       // %
    inline constexpr auto domOut     = "domOut";       // dB
    inline constexpr auto domDelta   = "domDelta";
    inline constexpr int domNumBands = 8;
    // The depth curve's bands: "domB1Used", "domB1Freq"... `what` is one of the names below.
    inline juce::String domBand (int band, const char* what) { return "domB" + juce::String (band + 1) + what; }
    inline constexpr const char* domBandSettings[] = { "Used", "On", "Shape", "Freq", "Depth", "Q", "Slope", "Focus" };

    // Stomp 7, the LIMITER (0.8.0): PR7-C's limiter and output trim, ported.
    // The last thing in the chain, after OUTPUT, RIG LEVEL and the volume pedal.
    inline constexpr auto limitOn        = "limitOn";
    inline constexpr auto limitThreshold = "limitThreshold";   // dBFS
    inline constexpr auto limitMakeUp    = "limitMakeUp";      // dB
    inline constexpr auto limitMode      = "limitMode";        // choice: Brick, Soft, Auto
    inline constexpr auto limitMix       = "limitMix";         // %
    inline constexpr auto limitLink      = "limitLink";        // both channels together
    inline constexpr auto limitTrim      = "limitTrim";        // OUTPUT TRIM, dB, after the limiter
    // From 0.16.0, inspired by Pro-L 2 (version hint 36): GAIN drives into the
    // OUTPUT ceiling. limitThreshold and limitMakeUp became GAIN and OUTPUT
    // (projects and rigs from before are converted when they load); they're
    // kept only so old automation and files still find them.
    inline constexpr auto limitGain          = "limitGain";          // dB
    inline constexpr auto limitCeiling       = "limitCeiling";       // OUTPUT, dBFS (dBTP with TRUE PEAK)
    inline constexpr auto limitLookahead     = "limitLookahead";     // ms
    inline constexpr auto limitAttack        = "limitAttack";        // ms, the release stage
    inline constexpr auto limitRelease       = "limitRelease";       // ms
    inline constexpr auto limitLinkTransient = "limitLinkTransient"; // %
    inline constexpr auto limitLinkRelease   = "limitLinkRelease";   // %
    inline constexpr auto limitTruePeak      = "limitTruePeak";
    inline constexpr auto limitOversampling  = "limitOversampling";  // choice: Off, 2x, 4x, 8x
    inline constexpr auto limitDither        = "limitDither";        // choice: Off, 24 ... 16 bits
    inline constexpr auto limitShaping       = "limitShaping";       // choice: None, Basic, Optimized, Weighted
    inline constexpr auto limitUnity         = "limitUnity";         // listening aids: not in rigs
    inline constexpr auto limitDelta         = "limitDelta";
    inline constexpr auto limitDcFilter      = "limitDcFilter";


    // THE SENDS (stage 7b, 0.18.0): PR7-G 0.52.0's DELAY and REVERB, with PR7-G's IDs, ranges and defaults; each
    // oscillator's SendDelay / SendReverb (oscSettings) feeds them, and their MIX is the return's level.
    inline constexpr auto delayOn        = "delayOn";
    inline constexpr auto delayTime      = "delayTime";       // ms (SYNC off)
    inline constexpr auto delaySync      = "delaySync";       // choice: effects::Delay::syncNames
    inline constexpr auto delayFeedback  = "delayFeedback";   // 0 to 0.95
    inline constexpr auto delayTone      = "delayTone";       // 0 to 10
    inline constexpr auto delayMix       = "delayMix";        // 0 to 1: the return's level
    inline constexpr auto delayPingPong  = "delayPingPong";
    inline constexpr auto delayType      = "delayType";       // choice: effects::Delay::typeNames
    inline constexpr auto delayDuck      = "delayDuck";       // 0 to 1
    inline constexpr auto reverbOn       = "reverbOn";
    inline constexpr auto reverbSize     = "reverbSize";      // 0 to 1
    inline constexpr auto reverbTone     = "reverbTone";      // 0 to 10
    inline constexpr auto reverbMix      = "reverbMix";       // 0 to 1: the return's level
    inline constexpr auto reverbType     = "reverbType";      // choice: effects::Reverb::typeNames
    inline constexpr auto reverbPreDelay = "reverbPreDelay";  // ms
    inline constexpr auto reverbDuck     = "reverbDuck";      // 0 to 1
    inline constexpr auto delayToReverb  = "delayToReverb";   // PR7-K's own: how much of DELAY's echoes go on into REVERB

    // MOD, MOD 2 and LO-FI (stage 7c, version hint 18): PR7-G 0.52.0's IDs, ranges and defaults, so the same names
    // mean the same knobs across the family. Not PR7-G's MOD POSITION: PR7-K has no amp to put them before.
    inline constexpr auto modOn      = "modOn";
    inline constexpr auto modType    = "modType";      // choice: effects::Modulation::typeNames
    inline constexpr auto modRate    = "modRate";      // Hz (SYNC off)
    inline constexpr auto modDepth   = "modDepth";     // 0 to 1
    inline constexpr auto modMix     = "modMix";       // 0 to 1 (E.LEVEL for the Vortex)
    inline constexpr auto modSync    = "modSync";      // choice: effects::Delay::syncNames ("Off" first)
    inline constexpr auto modEq      = "modEq";        // the Vortex's EQ, -1 to 1
    inline constexpr auto modStereo  = "modStereo";    // the Vortex's STEREO
    inline constexpr auto modManual  = "modManual";    // AEON's MANUAL, 0 to 10
    inline constexpr auto modRes     = "modRes";       // AEON's RES, 0 to 10
    inline constexpr auto modHadron  = "modHadron";    // Hadron's SCRIPT / BLOCK circuit
    inline constexpr auto modWave    = "modWave";      // Razor's WAVE, 0 to 10
    inline constexpr auto mod2On     = "mod2On";
    inline constexpr auto mod2Type   = "mod2Type";
    inline constexpr auto mod2Rate   = "mod2Rate";
    inline constexpr auto mod2Depth  = "mod2Depth";
    inline constexpr auto mod2Mix    = "mod2Mix";
    inline constexpr auto mod2Sync   = "mod2Sync";
    inline constexpr auto mod2Eq     = "mod2Eq";
    inline constexpr auto mod2Stereo = "mod2Stereo";
    inline constexpr auto mod2Manual = "mod2Manual";
    inline constexpr auto mod2Res    = "mod2Res";
    inline constexpr auto mod2Hadron = "mod2Hadron";
    inline constexpr auto mod2Wave   = "mod2Wave";

    // A MOD block's controls, so MOD and MOD 2 share their code (as on PR7-G).
    struct ModIds
    {
        const char *on, *type, *rate, *depth, *mix, *sync, *eq, *stereo, *manual, *res, *hadron, *wave;
    };
    inline constexpr ModIds modIds { modOn, modType, modRate, modDepth, modMix, modSync, modEq, modStereo, modManual, modRes, modHadron, modWave };
    inline constexpr ModIds mod2Ids { mod2On, mod2Type, mod2Rate, mod2Depth, mod2Mix, mod2Sync, mod2Eq, mod2Stereo, mod2Manual, mod2Res, mod2Hadron, mod2Wave };

    inline constexpr auto lofiOn     = "lofiOn";
    inline constexpr auto lofiType   = "lofiType";     // choice: effects::LoFi::typeNames
    inline constexpr auto lofiAmount = "lofiAmount";   // 0 to 10
    inline constexpr auto lofiNoise  = "lofiNoise";    // 0 to 10
    inline constexpr auto lofiMix    = "lofiMix";      // 0 to 1
    inline constexpr auto lofiLevel  = "lofiLevel";    // dB

    // THE INSERT BUSES (stage 7d, version hint 19): A to D, each "ins1"... "ins4" plus a setting below: PR7-G's
    // DRIVE and GLITCH (their IDs after the prefix, capitalised), then MOD and LO-FI as above, then the bus's SENDS.
    inline constexpr int numInsertBuses = 4;
    inline juce::String ins (int bus, const char* what) { return "ins" + juce::String (bus + 1) + what; }
    inline const juce::StringArray insertBusNames { "None", "A", "B", "C", "D" };   // osc{n}Insert, by index: only append
    inline constexpr const char* insDriveSettings[] = { "DriveOn", "DriveModel", "DriveGain", "DriveTone", "DriveLevel", "DriveAttack",
                                                        "DriveGate", "DriveHigh", "DriveLow", "DriveMiddle", "DriveMidFreq" };
    inline constexpr const char* insGlitchSettings[] = { "GlitchOn", "GlitchMode", "GlitchSync", "GlitchTime", "GlitchDivision",
                                                         "GlitchDensity", "GlitchPitch", "GlitchChaos", "GlitchMix" };
    inline constexpr const char* insModSettings[] = { "ModOn", "ModType", "ModRate", "ModDepth", "ModMix", "ModSync", "ModEq", "ModStereo",
                                                      "ModManual", "ModRes", "ModHadron", "ModWave" };   // ModIds' order
    inline constexpr const char* insLofiSettings[] = { "LofiOn", "LofiType", "LofiAmount", "LofiNoise", "LofiMix", "LofiLevel" };
    // A bus's MOD as ModIds (the strings made once and kept).
    inline const ModIds& insModIds (int bus)
    {
        static const auto table = []
        {
            std::array<std::array<juce::String, std::size (insModSettings)>, numInsertBuses> names;
            for (int b = 0; b < numInsertBuses; ++b)
                for (size_t k = 0; k < std::size (insModSettings); ++k)
                    names[(size_t) b][k] = ins (b, insModSettings[k]);
            return names;
        }();
        static const auto ids = []
        {
            std::array<ModIds, numInsertBuses> result {};
            for (size_t b = 0; b < (size_t) numInsertBuses; ++b)
            {
                const auto& n = table[b];
                result[b] = { n[0].toRawUTF8(), n[1].toRawUTF8(), n[2].toRawUTF8(), n[3].toRawUTF8(), n[4].toRawUTF8(), n[5].toRawUTF8(),
                              n[6].toRawUTF8(), n[7].toRawUTF8(), n[8].toRawUTF8(), n[9].toRawUTF8(), n[10].toRawUTF8(), n[11].toRawUTF8() };
            }
            return result;
        }();
        return ids[(size_t) juce::jlimit (0, numInsertBuses - 1, bus)];
    }

    // Global settings: kept when switching presets. Everything else is part of a preset.
    // The LIMITER's UNITY and DELTA are for listening while you set it, not part of a sound.
    inline const juce::StringArray notInRigs { outputGain, bypass, tune, mwBreath, mwExpression, mwAftertouch, mwSustain, limitUnity, limitDelta };
}

namespace ParamRanges
{
    // COMP's, as on PR7-C.
    inline constexpr float compThresholdMin = -50.0f, compThresholdMax = 0.0f;
    inline constexpr float compAttackMin = 0.1f, compAttackMax = 30.0f;          // ms
    inline constexpr float compReleaseMin = 50.0f, compReleaseMax = 3000.0f;     // ms
    inline constexpr float compMakeUpMax = 20.0f;
    inline const juce::StringArray compCharacters { "VCA", "OPTO", "FET" };    // saved by index: only append
    inline constexpr float compRatioMarks[] = { 1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 30.0f };   // SOFT, then to infinite

    // The LIMITER's, as on PR7-C.
    inline constexpr float limitThresholdMin = -20.0f, limitMakeUpMax = 12.0f;
    inline constexpr float limitTrimMin = -12.0f, limitTrimMax = 12.0f;
    // Its styles: PR7-C's three, then (0.16.0) eight of our own after Pro-L 2's (effects::limiter::Style).
    inline const juce::StringArray limitModes { "Brick", "Soft", "Auto",   // saved by index: only append
                                                "Clear", "Punchy", "Dynamic", "All-round", "Aggressive", "Modern", "Glue", "Safe" };
    inline constexpr int limitDefaultMode = 8;   // Modern
    inline constexpr float limitGainMax = 30.0f, limitCeilingMin = -30.0f;

    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 12.0f;
    inline constexpr float outputDefaultDb = -6.0f;   // headroom for chords (Avenger's master gain defaults to -3 dB)
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
void addRackParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp
void addSendParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7b)
void addModFxParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7c)
void addInsertParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);   // RackParameters.cpp (stage 7d)
