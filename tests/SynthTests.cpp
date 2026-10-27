// The synth engine (stage 1): what each part does to the sound, measured.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_dsp/juce_dsp.h>

#include "dsp/synth/Engine.h"
#include "dsp/synth/WaveTables.h"
#include "ui/SynthDisplays.h"

#include "SynthTestUtils.h"

//==============================================================================
TEST_CASE ("Wave tables: each level holds a third of an octave fewer harmonics, all under the limit", "[synth][tables]")
{
    using namespace synth::tables;
    CHECK (harmonicsAt (0) == 512);
    CHECK (harmonicsAt (3) == 256);
    CHECK (harmonicsAt (numLevels - 1) == 1);
    for (int level = 1; level < numLevels; ++level)
        CHECK (harmonicsAt (level) <= harmonicsAt (level - 1));

    // The chosen level's top harmonic stays under the limit, and the next richer one wouldn't.
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (double hz = 20.0; hz < 15000.0; hz *= 1.07)
        {
            const auto level = levelFor (hz, rate);
            INFO (hz << " Hz at " << rate);
            CHECK (harmonicsAt (level) * hz <= harmonicLimitHz (rate) + 1.0e-6);
            if (level > 0)
                CHECK (harmonicsAt (level - 1) * hz > harmonicLimitHz (rate));
        }
    CHECK_THAT (harmonicLimitHz (48000.0), WithinAbs (28000.0, 1.0e-9));   // folds back no lower than 20 kHz
}

TEST_CASE ("Every wave plays in tune: A4 is 440 Hz, TRANSPOSE and FINE move it", "[synth][pitch]")
{
    for (const auto wave : { synth::OscWave::saw, synth::OscWave::pulse, synth::OscWave::triangle, synth::OscWave::sine })
    {
        Player p;
        p.osc().wave = wave;
        p.on (69);
        const auto out = p.render (1.6);
        INFO (synth::oscWaveNames[(int) wave]);
        // The fundamental's peak in the spectrum, placed between bins by the curve through the top three.
        const Spectrum spectrum (out[0], 4096);
        auto peak = (int) (300.0 / Spectrum::binHz);
        for (auto bin = peak; bin < (int) (600.0 / Spectrum::binHz); ++bin)
            if (spectrum.db[(size_t) bin] > spectrum.db[(size_t) peak])
                peak = bin;
        const auto a = spectrum.db[(size_t) peak - 1], b = spectrum.db[(size_t) peak], c = spectrum.db[(size_t) peak + 1];
        const auto hz = ((double) peak + 0.5 * (a - c) / (a - 2.0 * b + c)) * Spectrum::binHz;
        CHECK_THAT (hz, WithinAbs (440.0, 0.05));
    }

    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.osc().transpose = 12.0f;
    p.osc().fineCents = 50.0f;
    p.on (57);   // A3, an octave and a quarter tone up: A4 + 50 cents
    const auto out = p.render (1.0);
    CHECK_THAT (frequencyOf (out[0], 24000, 48000), WithinAbs (440.0 * std::pow (2.0, 50.0 / 1200.0), 0.05));
}

TEST_CASE ("The saw is band-limited: nothing folds back between its harmonics, unlike a plain ramp", "[synth][aliasing]")
{
    // E7 (2637 Hz) at 48 kHz: a plain ramp's harmonics above 24 kHz fold back
    // all over the audible range. Ours has nothing there.
    Player p;
    p.on (100);
    const auto out = p.render (1.6);
    const Spectrum ours (out[0], 4096);
    const auto f0 = noteHz (100);
    const auto worst = ours.loudestBetweenHarmonics (f0, 20000.0);
    WARN ("saw at 2.6 kHz, loudest between harmonics: " << worst << " dB");
    CHECK (worst < -90.0);

    // The same note as a plain ramp, for comparison.
    std::vector<float> naive (out[0].size());
    double phase = 0.0;
    for (auto& x : naive)
    {
        x = (float) (2.0 * phase - 1.0);
        phase += f0 / sampleRate;
        phase -= std::floor (phase);
    }
    const Spectrum plain (naive, 4096);
    WARN ("the same note as a plain ramp: " << plain.loudestBetweenHarmonics (f0, 20000.0) << " dB");
    CHECK (plain.loudestBetweenHarmonics (f0, 20000.0) > -40.0);

    // And it keeps its top: harmonics up to 20 kHz are all there (7 of them).
    for (int h = 1; h * f0 < 20000.0; ++h)
        CHECK (ours.at (h * f0) > db (1.0 / h) - 1.5);
}

TEST_CASE ("PULSE: 50% WIDTH is a square (no even harmonics); 25% has no 4th", "[synth][pulse]")
{
    const auto harmonics = [] (float width)
    {
        Player p;
        p.osc().wave = synth::OscWave::pulse;
        p.osc().pulseWidth = width;
        p.on (45);   // 110 Hz
        return p.render (1.0)[0];
    };
    const auto square = harmonics (0.5f);
    const auto h1 = levelAt (square, 110.0, 24000, 24000);
    CHECK (db (levelAt (square, 220.0, 24000, 24000) / h1) < -60.0);
    CHECK (db (levelAt (square, 440.0, 24000, 24000) / h1) < -60.0);
    CHECK_THAT (db (levelAt (square, 330.0, 24000, 24000) / h1), WithinAbs (db (1.0 / 3.0), 0.3));   // odd ones at 1/n

    const auto quarter = harmonics (0.25f);
    const auto q1 = levelAt (quarter, 110.0, 24000, 24000);
    CHECK (db (levelAt (quarter, 440.0, 24000, 24000) / q1) < -60.0);
    CHECK (db (levelAt (quarter, 220.0, 24000, 24000) / q1) > -10.0);
}

TEST_CASE ("SUB: a sine an octave (or two) under the note, at its level; INVERT flips it", "[synth][sub]")
{
    Player p;
    p.osc().gain = 0.0f;   // the sub alone
    p.osc().sub = 0.5f;
    p.osc().subOctave = 1;   // -1 octave
    p.on (69);
    auto out = p.render (1.0);
    CHECK_THAT (levelAt (out[0], 220.0, 24000, 24000), WithinAbs (0.5, 0.01));
    CHECK_THAT (frequencyOf (out[0], 24000, 48000), WithinAbs (220.0, 0.05));

    Player two;
    two.osc().gain = 0.0f;
    two.osc().sub = 1.0f;
    two.osc().subOctave = 0;   // -2 octaves
    two.on (69);
    CHECK_THAT (frequencyOf (two.render (1.0)[0], 24000, 48000), WithinAbs (110.0, 0.05));

    Player flipped;
    flipped.osc().gain = 0.0f;
    flipped.osc().sub = 0.5f;
    flipped.osc().subInvert = true;
    flipped.on (69);
    const auto inverted = flipped.render (1.0);
    for (size_t i = 1000; i < 1100; ++i)
        CHECK_THAT (inverted[0][i], WithinAbs (-out[0][i], 1.0e-5));
}

TEST_CASE ("NOISE: COLOUR goes from bright to dark at about the same loudness; WIDTH from stereo to mono", "[synth][noise]")
{
    const auto noise = [] (float colour, synth::NoiseWidth width)
    {
        Player p;
        p.osc().gain = 0.0f;
        p.osc().noise = 1.0f;
        p.osc().noiseColour = colour;
        p.osc().noiseWidth = width;
        p.on (60);
        return p.render (1.6);   // enough for a 65536-sample spectrum
    };
    // Brightness: how much is above 5 kHz compared with below 500 Hz.
    const auto tilt = [] (const std::vector<float>& x)
    {
        const Spectrum s (x, 0);
        double low = 0.0, high = 0.0;
        for (size_t bin = 1; bin < s.db.size(); ++bin)
        {
            const auto hz = (double) bin * Spectrum::binHz;
            const auto power = std::pow (10.0, s.db[bin] / 10.0);
            if (hz < 500.0)
                low += power;
            else if (hz > 5000.0)
                high += power;
        }
        return 10.0 * std::log10 (high / low);
    };

    const auto white = noise (0.0f, synth::NoiseWidth::stereo), pink = noise (0.5f, synth::NoiseWidth::stereo),
               brown = noise (1.0f, synth::NoiseWidth::stereo);
    CHECK (tilt (white[0]) > tilt (pink[0]) + 6.0);
    CHECK (tilt (pink[0]) > tilt (brown[0]) + 6.0);
    // About the same loudness whatever the colour (within 4 dB).
    const auto whiteLevel = rms (white[0], 0, 48000);
    CHECK (std::abs (db (rms (pink[0], 0, 48000) / whiteLevel)) < 4.0);
    CHECK (std::abs (db (rms (brown[0], 0, 48000) / whiteLevel)) < 4.0);

    // Stereo: the two sides differ; mono: identical.
    const auto correlation = [] (const std::array<std::vector<float>, 2>& x)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = 0; i < x[0].size(); ++i)
        {
            lr += (double) x[0][i] * x[1][i];
            ll += (double) x[0][i] * x[0][i];
            rr += (double) x[1][i] * x[1][i];
        }
        return lr / std::sqrt (ll * rr);
    };
    CHECK (std::abs (correlation (white)) < 0.05);
    CHECK (correlation (noise (0.0f, synth::NoiseWidth::half)) > 0.5);
    CHECK_THAT (correlation (noise (0.0f, synth::NoiseWidth::mono)), WithinAbs (1.0, 1.0e-9));
}

//==============================================================================
TEST_CASE ("AMP envelope: ATTACK, HOLD, DECAY to SUSTAIN, RELEASE take the times they say", "[synth][envelope]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.amp.envelope = { 100.0f, 50.0f, 200.0f, 0.5f, 300.0f };
    p.on (69);
    const auto held = p.render (1.0);
    const auto levelAround = [] (const std::vector<float>& x, double ms)
    {
        const auto at = (size_t) (ms * 0.001 * sampleRate);
        return levelAt (x, 440.0, at - 240, 480);   // 10 ms round it
    };
    CHECK_THAT (levelAround (held[0], 50.0), WithinAbs (0.5, 0.03));     // half way up the (straight) attack
    CHECK_THAT (levelAround (held[0], 125.0), WithinAbs (1.0, 0.03));    // holding at the top
    CHECK_THAT (levelAround (held[0], 250.0), WithinAbs (0.75, 0.03));   // half way down the decay
    CHECK_THAT (levelAround (held[0], 800.0), WithinAbs (0.5, 0.02));    // sustaining

    p.off (69);
    const auto released = p.render (0.5);
    CHECK_THAT (levelAround (released[0], 150.0), WithinAbs (0.25, 0.03));   // half way through the release
    CHECK (rms (released[0], 15000, 24000) < 1.0e-6);
    CHECK (p.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("Envelope curves: negative falls fast then slows, positive the other way", "[synth][envelope]")
{
    const auto quarterThroughRelease = [] (float curve)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope = { 0.0f, 0.0f, 0.0f, 1.0f, 400.0f, 0.0f, 0.0f, curve };
        p.on (69);
        p.render (0.2);
        p.off (69);
        const auto out = p.render (0.4);
        return levelAt (out[0], 440.0, 4800 - 240, 480);   // 100 ms in
    };
    CHECK_THAT (quarterThroughRelease (0.0f), WithinAbs (0.75, 0.03));
    CHECK (quarterThroughRelease (-0.6f) < 0.5);
    CHECK (quarterThroughRelease (0.6f) > 0.9);
}

TEST_CASE ("A note ends as soon as it can't be heard (below -90 dB), before a long release is over", "[synth][voices][performance]")
{
    // A 20-second release, bent all the way (an exponential fade, as natural
    // sounds die away): its last second is below -90 dB, and costs nothing here.
    Player p;
    p.settings.amp.envelope = { 1.0f, 0.0f, 0.0f, 1.0f, 20000.0f, 0.0f, 0.0f, -1.0f };
    p.on (60);
    p.render (0.1);
    p.off (60);
    double seconds = 0.0;
    while (p.engine.getNumActiveVoices() > 0 && seconds < 21.0)
    {
        p.render (0.1);
        seconds += 0.1;
    }
    INFO ("ended " << seconds << " s into a 20 s release");
    CHECK (seconds < 19.5);   // its last second is below -90 dB

    // And a pluck that decays to a SUSTAIN of zero ends then, though the key is still held.
    Player pluck;
    pluck.settings.amp.envelope = { 1.0f, 0.0f, 300.0f, 0.0f, 500.0f };
    pluck.on (60);
    pluck.render (0.4);
    CHECK (pluck.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("AMP: VELOCITY scales the level; SPIKE adds a click at the start; PAN and SPREAD place notes", "[synth][amp]")
{
    const auto level = [] (float velocityAmount, float velocity)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.velocity = velocityAmount;
        p.on (69, velocity);
        return levelAt (p.render (0.5)[0], 440.0, 12000, 12000);
    };
    CHECK_THAT (level (1.0f, 0.5f) / level (1.0f, 1.0f), WithinAbs (0.5, 0.01));
    CHECK_THAT (level (0.0f, 0.2f) / level (0.0f, 1.0f), WithinAbs (1.0, 0.01));

    Player spiked;
    spiked.osc().wave = synth::OscWave::sine;
    spiked.settings.amp.spike = 1.0f;
    spiked.settings.amp.envelope.attackMs = 0.0f;
    spiked.on (69);
    const auto out = spiked.render (0.2);
    CHECK (levelAt (out[0], 440.0, 0, 240) > 2.0);                              // the first few ms: up to 4 times
    CHECK_THAT (levelAt (out[0], 440.0, 2400, 2400), WithinAbs (1.0, 0.02));    // gone after 20 ms

    Player spread;
    spread.osc().wave = synth::OscWave::sine;
    spread.settings.amp.spread = 1.0f;   // alternating, fully
    spread.on (60);
    auto first = spread.render (0.2);
    spread.off (60);
    spread.render (0.5);
    spread.on (60);
    auto second = spread.render (0.2);
    const auto side = [] (const std::array<std::vector<float>, 2>& x) { return rms (x[1], 4800, 9600) - rms (x[0], 4800, 9600); };
    CHECK (side (first) * side (second) < 0.0);   // one to each side
    CHECK (std::min (rms (first[0], 4800, 9600), rms (first[1], 4800, 9600)) < 1.0e-6);   // fully over
}

//==============================================================================
TEST_CASE ("FILTER: what each type does to the sound matches the curve on its display", "[synth][filter]")
{
    // A sine swept through each type: its level after the filter, against the display's curve.
    for (int t = 0; t < synth::filterTypeNames.size(); ++t)
    {
        const auto type = (synth::FilterType) t;
        for (const auto [cutoff, resonance] : { std::pair { 1000.0f, 0.0f }, std::pair { 600.0f, 0.5f } })
            for (const auto note : { 45, 57, 64, 69, 76, 81, 93, 100 })   // 110 Hz to 2.6 kHz
            {
                Player p;
                p.osc().wave = synth::OscWave::sine;
                p.settings.filter.on = true;
                p.settings.filter.type = type;
                p.settings.filter.cutoffHz = cutoff;
                p.settings.filter.resonance = resonance;
                p.on (note);
                const auto hz = noteHz (note);
                const auto measured = db (levelAt (p.render (0.6)[0], hz, 14400, 14400));
                const auto expected = FilterDisplay::responseDb (type, cutoff, resonance, hz);
                INFO (synth::filterTypeNames[t] << " at " << cutoff << " Hz, resonance " << resonance << ": " << hz << " Hz");
                if (expected > -60.0)
                    CHECK_THAT (measured, WithinAbs (expected, 0.75));
                else
                    CHECK (measured < -55.0);
            }
    }
}

TEST_CASE ("FILTER slopes: two octaves above the cutoff, each low-pass is down by about its slope", "[synth][filter]")
{
    // Two octaves past the corner: 6 dB/oct types lose about 12 dB, 24 dB/oct about 48 (plus the corner's own few dB).
    const std::pair<synth::FilterType, double> slopes[] = { { synth::FilterType::lp6, 12.0 }, { synth::FilterType::lp12, 24.0 },
                                                            { synth::FilterType::lp18, 36.0 }, { synth::FilterType::lp24, 48.0 } };
    for (const auto& [type, drop] : slopes)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.filter.on = true;
        p.settings.filter.type = type;
        p.settings.filter.cutoffHz = (float) noteHz (57);   // 220 Hz
        p.on (81);                                          // 880 Hz
        const auto measured = db (levelAt (p.render (0.6)[0], 880.0, 14400, 14400));
        INFO (synth::filterTypeNames[(int) type] << ": " << measured << " dB");
        CHECK (measured < -drop + 1.5);
        CHECK (measured > -drop - 8.0);
    }
}

TEST_CASE ("FILTER: RESONANCE makes a peak at the cutoff; KEYTRACK makes the cutoff follow the note", "[synth][filter]")
{
    const auto atCutoff = [] (float resonance)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.filter.on = true;
        p.settings.filter.type = synth::FilterType::lp12;
        p.settings.filter.cutoffHz = 440.0f;
        p.settings.filter.resonance = resonance;
        p.on (69);
        return db (levelAt (p.render (0.6)[0], 440.0, 14400, 14400));
    };
    CHECK_THAT (atCutoff (0.0f), WithinAbs (-6.0, 0.3));   // no resonance: Q 0.5
    CHECK (atCutoff (0.8f) > 15.0);                         // a strong peak

    // KEYTRACK 100%: an octave up the keyboard, the cutoff an octave up too, so
    // each note's own pitch is cut the same.
    const auto ownLevel = [] (int note)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.filter.on = true;
        p.settings.filter.type = synth::FilterType::lp24;
        p.settings.filter.cutoffHz = 300.0f;
        p.settings.filter.keytrack = 1.0f;
        p.on (note);
        return db (levelAt (p.render (0.6)[0], noteHz (note), 14400, 14400));
    };
    CHECK_THAT (ownLevel (72), WithinAbs (ownLevel (60), 0.3));
    CHECK_THAT (ownLevel (84), WithinAbs (ownLevel (60), 0.3));
}

TEST_CASE ("FILTER envelope: a positive amount opens it as the note starts, then closes as it decays", "[synth][filter]")
{
    Player p;
    p.settings.filter.on = true;
    p.settings.filter.type = synth::FilterType::lp24;
    p.settings.filter.cutoffHz = 200.0f;
    p.settings.filter.envelopeAmount = 0.5f;   // +4 octaves at the peak
    p.settings.filter.envelope = { 0.0f, 0.0f, 300.0f, 0.0f, 100.0f };
    p.on (45);   // a 110 Hz saw
    const auto out = p.render (1.0);
    // The 8th harmonic (880 Hz): loud while the filter's open, far quieter once closed.
    const auto early = db (levelAt (out[0], 880.0, 0, 2400));
    const auto late = db (levelAt (out[0], 880.0, 36000, 9600));
    CHECK (early > late + 20.0);
}

TEST_CASE ("FILTER velocity: softer notes are darker, by as much as VELOCITY says", "[synth][filter]")
{
    const auto brightness = [] (float velocity)
    {
        Player p;
        p.settings.filter.on = true;
        p.settings.filter.type = synth::FilterType::lp24;
        p.settings.filter.cutoffHz = 2000.0f;
        p.settings.filter.velocity = 1.0f;
        p.settings.amp.velocity = 0.0f;   // the same level, so only the tone differs
        p.on (45, velocity);
        return db (levelAt (p.render (0.6)[0], 1760.0, 14400, 14400));   // the 16th harmonic
    };
    CHECK (brightness (1.0f) > brightness (0.5f) + 12.0);
}

TEST_CASE ("DRIVE saturates: harmonics on a sine, without the folded-back tones of a plain clipper", "[synth][filter][aliasing]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.filter.on = true;
    p.settings.filter.type = synth::FilterType::lp24;
    p.settings.filter.cutoffHz = 20000.0f;
    p.settings.filter.drive = 1.0f;
    p.on (93);   // 1760 Hz: its 3rd, 5th... harmonics run past half the sample rate
    const auto out = p.render (1.6);
    const auto f0 = noteHz (93);
    CHECK (db (levelAt (out[0], 3.0 * f0, 24000, 24000) / levelAt (out[0], f0, 24000, 24000)) > -30.0);   // it distorts
    const Spectrum ours (out[0], 4096);

    // The same sine through a plain tanh with the same push, for comparison.
    std::vector<float> plain (out[0].size());
    const auto push = 16.0;
    for (size_t i = 0; i < plain.size(); ++i)
        plain[i] = (float) (std::tanh (push * std::sin (juce::MathConstants<double>::twoPi * f0 * (double) i / sampleRate)) / std::tanh (push));
    const Spectrum clipped (plain, 4096);

    const auto oursWorst = ours.loudestBetweenHarmonics (f0, 18000.0), plainWorst = clipped.loudestBetweenHarmonics (f0, 18000.0);
    WARN ("DRIVE on a 1.76 kHz sine, loudest folded tone: " << oursWorst << " dB; a plain tanh: " << plainWorst << " dB");
    // First-order anti-aliasing: about 10 dB less folding than a plain clipper
    // here (a 1.76 kHz sine pushed 24 dB into it). Not none: very hard
    // drive on high notes still folds a little, as on most synths' filters.
    CHECK (oursWorst < plainWorst - 8.0);
    CHECK (oursWorst < -38.0);
}

//==============================================================================
TEST_CASE ("POLY: VOICES limits the notes at once; the quietest fading note makes way first", "[synth][voices]")
{
    Player p;
    p.settings.play.voices = 4;
    p.settings.amp.envelope.releaseMs = 2000.0f;
    for (const auto note : { 48, 52, 55, 59 })
        p.on (note);
    p.render (0.2);
    p.off (48);   // fading out over 2 s
    p.render (0.5);
    p.on (62);    // a fifth note: the fading one makes way
    p.render (0.1);
    CHECK (p.engine.getNumActiveVoices() == 4);
    p.on (64);    // a sixth: now the oldest held one goes
    p.render (0.1);
    CHECK (p.engine.getNumActiveVoices() == 4);
}

TEST_CASE ("POLY: a stolen note fades out in 3 ms, not a click", "[synth][voices]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.play.voices = 1;
    p.on (45);
    p.render (0.2);
    p.on (69);
    const auto out = p.render (0.2);
    auto largestStep = 0.0f;
    for (size_t i = 1; i < 960; ++i)
        largestStep = std::max (largestStep, std::abs (out[0][i] - out[0][i - 1]));
    CHECK (largestStep < 0.08f);   // a 440 Hz sine at full level moves up to 0.058 a sample
}

TEST_CASE ("MONO: the last key plays; LOW and HIGH play the lowest or highest held; letting go goes back", "[synth][mono]")
{
    const auto pitchOf = [] (Player& p) { return frequencyOf (p.render (0.4)[0], 9600, 19200); };
    for (const auto mode : { synth::PlayMode::mono, synth::PlayMode::monoLow, synth::PlayMode::monoHigh })
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.play.mode = mode;
        p.on (60);
        p.on (67);
        p.on (64);
        INFO (synth::playModeNames[(int) mode]);
        const auto expected = mode == synth::PlayMode::mono ? 64 : mode == synth::PlayMode::monoLow ? 60 : 67;
        CHECK_THAT (pitchOf (p), WithinAbs (noteHz (expected), 0.1));
        CHECK (p.engine.getNumActiveVoices() == 1);

        p.off (expected);
        const auto back = mode == synth::PlayMode::mono ? 67 : mode == synth::PlayMode::monoLow ? 64 : 64;
        CHECK_THAT (pitchOf (p), WithinAbs (noteHz (back), 0.1));
    }
}

TEST_CASE ("MONO LEGATO: an overlapping note carries on without restarting the envelope; GLIDE slides to it", "[synth][mono]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.play.mode = synth::PlayMode::mono;
    p.settings.play.legato = true;
    p.settings.play.glideMs = 100.0f;
    p.settings.play.glideMode = synth::GlideMode::legato;
    p.settings.amp.envelope = { 300.0f, 0.0f, 0.0f, 1.0f, 100.0f };
    p.on (57);
    p.render (0.15);   // half way up the attack
    p.on (69);         // an octave up, overlapping
    const auto out = p.render (0.4);
    // The attack carries on from half way (no restart at zero).
    CHECK (levelAt (out[0], 220.0, 0, 480) + levelAt (out[0], 440.0, 0, 480) > 0.35);
    // Half way through the glide, half way there (in pitch).
    CHECK_THAT (frequencyOf (out[0], 2160, 2640), WithinAbs (noteHz (63), 8.0));
    // Arrived after 100 ms.
    CHECK_THAT (frequencyOf (out[0], 7200, 19200), WithinAbs (440.0, 0.1));

    // Not overlapping: with GLIDE ON LEGATO, no slide.
    p.off (57);
    p.off (69);
    p.render (0.5);
    p.on (57);
    CHECK_THAT (frequencyOf (p.render (0.1)[0], 480, 4800), WithinAbs (220.0, 0.2));
}

TEST_CASE ("An oscillator's own VOICES limit: 1 keeps that oscillator to the newest note", "[synth][voices]")
{
    Player p;
    p.osc (0).wave = synth::OscWave::sine;
    p.osc (0).volume = 0.0f;   // oscillator 1 silent, so only oscillator 2 is heard
    p.osc (1).on = true;
    p.osc (1).randomPhase = false;
    p.osc (1).wave = synth::OscWave::sine;
    p.osc (1).voiceLimit = 1;
    p.on (57);
    p.render (0.1);
    p.on (64);
    const auto out = p.render (0.5);
    CHECK (levelAt (out[0], noteHz (64), 9600, 9600) > 0.9);
    CHECK (levelAt (out[0], noteHz (57), 9600, 9600) < 0.01);   // taken by the newer note (what's left is the newer note's leakage into the measurement)
    CHECK (p.engine.getNumActiveVoices() == 2);                  // the older note still runs its other oscillator
}

TEST_CASE ("The sustain pedal holds notes after their keys are let go", "[synth][midi]")
{
    Player p;
    p.midi (juce::MidiMessage::controllerEvent (1, 64, 127));
    p.on (60);
    p.render (0.1);
    p.off (60);
    p.render (0.5);
    CHECK (p.engine.getNumActiveVoices() == 1);
    p.midi (juce::MidiMessage::controllerEvent (1, 64, 0));
    p.render (1.0);
    CHECK (p.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("The pitch wheel bends by BEND UP and BEND DOWN", "[synth][midi]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.play.bendUp = 2.0f;
    p.settings.play.bendDown = 12.0f;
    p.on (69);
    p.midi (juce::MidiMessage::pitchWheel (1, 16383));
    // The wheel's top is 8191 steps above centre, of 8192 to a full bend.
    CHECK_THAT (frequencyOf (p.render (0.5)[0], 4800, 24000), WithinAbs (440.0 * std::pow (2.0, 2.0 * 8191.0 / 8192.0 / 12.0), 0.1));
    p.midi (juce::MidiMessage::pitchWheel (1, 0));
    CHECK_THAT (frequencyOf (p.render (0.5)[0], 4800, 24000), WithinAbs (220.0, 0.1));
}

TEST_CASE ("RANDOM: alternating goes sharp then flat; PHASE fixed starts every note the same", "[synth][pitch]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.osc().randomPitch = 0.5f;   // alternating, half a semitone
    std::vector<double> pitches;
    for (int i = 0; i < 4; ++i)
    {
        p.on (69);
        pitches.push_back (frequencyOf (p.render (0.3)[0], 2400, 14400));
        p.off (69);
        p.render (0.4);
    }
    CHECK_THAT (pitches[0], WithinAbs (noteHz (69.5), 0.1));
    CHECK_THAT (pitches[1], WithinAbs (noteHz (68.5), 0.1));
    CHECK_THAT (pitches[2], WithinAbs (noteHz (69.5), 0.1));

    Player fixed;
    fixed.osc().phase = 0.25f;   // a quarter way in: a saw starts at -0.5
    fixed.settings.amp.envelope.attackMs = 0.0f;
    fixed.on (60);
    const auto first = fixed.render (0.01);
    CHECK_THAT (first[0][0], WithinAbs (-0.5, 0.05));
}

TEST_CASE ("Oscillators: VOLUME, PAN, GAIN, INVERT and TO FILTER", "[synth]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.osc().volume = 0.5f;
    p.osc().pan = 1.0f;   // hard right
    p.on (69);
    auto out = p.render (0.5);
    CHECK (rms (out[0], 12000, 24000) < 1.0e-9);
    CHECK_THAT (levelAt (out[1], 440.0, 12000, 12000), WithinAbs (0.5, 0.01));

    // Two copies, one inverted, cancel.
    Player cancel;
    for (int i = 0; i < 2; ++i)
    {
        cancel.osc (i).on = true;
        cancel.osc (i).randomPhase = false;
        cancel.osc (i).invert = i == 1;
    }
    cancel.on (60);
    CHECK (rms (cancel.render (0.3)[0], 0, 14400) < 1.0e-6);

    // TO FILTER off: it skips the filter, so a closed filter doesn't touch it.
    Player bypass;
    bypass.osc().wave = synth::OscWave::sine;
    bypass.osc().toFilter = false;
    bypass.settings.filter.on = true;
    bypass.settings.filter.cutoffHz = 20.0f;
    bypass.on (69);
    CHECK_THAT (levelAt (bypass.render (0.5)[0], 440.0, 12000, 12000), WithinAbs (1.0, 0.01));
}

//==============================================================================
TEST_CASE ("CPU: a heavy patch runs many times faster than real time; silence costs nothing", "[synth][performance]")
{
    // 8 notes, 4 saws each (32 oscillators), noise and a sub on one, through a
    // driven 24 dB filter with its envelope moving: a busy pad.
    Player p;
    for (int i = 0; i < 4; ++i)
    {
        p.osc (i).on = true;
        p.osc (i).fineCents = (float) (i * 5 - 7);
    }
    p.osc (0).noise = 0.2f;
    p.osc (0).sub = 0.5f;
    p.settings.filter.on = true;
    p.settings.filter.type = synth::FilterType::lp24;
    p.settings.filter.cutoffHz = 800.0f;
    p.settings.filter.drive = 0.3f;
    p.settings.filter.envelopeAmount = 0.4f;
    p.settings.filter.envelope = { 500.0f, 0.0f, 2000.0f, 0.3f, 1000.0f };
    for (const auto note : { 36, 43, 48, 52, 55, 59, 62, 67 })
        p.on (note);

    const auto started = juce::Time::getMillisecondCounterHiRes();
    p.render (10.0);
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 10000.0 * 100.0;
    WARN ("8 notes x 4 saws, filter and drive: " << percent << "% of one core in real time");
    CHECK (percent < 25.0);   // generous for slow CI machines; locally it's a few percent

    // No notes: (almost) nothing to do.
    Player idle;
    const auto idleStart = juce::Time::getMillisecondCounterHiRes();
    idle.render (10.0);
    const auto idlePercent = (juce::Time::getMillisecondCounterHiRes() - idleStart) / 10000.0 * 100.0;
    WARN ("no notes: " << idlePercent << "%");
    CHECK (idlePercent < 0.5);
}

//==============================================================================
// Stage 2: UNISON, the CHORDER and VIBRATO.
namespace
{
    // An A4 sine on oscillator 1, its unison set as given (no spread or detune unless asked).
    Player unisonSine (int voices, float mix, float detune = 0.0f, float spread = 0.0f)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.osc().unisonVoices = voices;
        p.osc().unisonMix = mix;
        p.osc().unisonDetune = detune;
        p.osc().unisonSpread = spread;
        return p;
    }

    double cents (double fromHz, double toHz) { return 1200.0 * std::log2 (toHz / fromHz); }

    // The pitch over time: the frequency in each `windowMs` stretch, from zero crossings.
    std::vector<double> pitchTrack (const std::vector<float>& x, double windowMs)
    {
        std::vector<double> track;
        const auto window = (size_t) (windowMs * 0.001 * sampleRate);
        for (size_t from = 0; from + window <= x.size(); from += window)
            track.push_back (frequencyOf (x, from, from + window));
        return track;
    }
}

TEST_CASE ("UNISON at MIX 0 is off: the sound is exactly as without it", "[synth][unison]")
{
    Player plain, off;
    off.osc().unisonVoices = 7;
    off.osc().unisonMix = 0.0f;
    off.osc().unisonDetune = 1.0f;
    off.osc().unisonSpread = 1.0f;
    plain.on (57);
    off.on (57);
    const auto a = plain.render (0.5), b = off.render (0.5);
    for (size_t i = 0; i < a[0].size(); ++i)
        REQUIRE (a[0][i] == b[0][i]);
    CHECK (off.engine.getNumActiveOscillators (off.settings) == 1);
}

TEST_CASE ("UNISON: copies at the pitches DETUNE says, at MIX's level", "[synth][unison]")
{
    // 3 copies, DETUNE 100%: the note, and copies a semitone (100 cents) either side, at half level.
    auto p = unisonSine (3, 0.5f, 1.0f);
    p.on (69);
    const auto out = p.render (1.0);
    CHECK_THAT (levelAt (out[0], 440.0, 0, 48000), WithinAbs (1.0, 0.03));
    CHECK_THAT (levelAt (out[0], noteHz (70), 0, 48000), WithinAbs (0.5, 0.03));
    CHECK_THAT (levelAt (out[0], noteHz (68), 0, 48000), WithinAbs (0.5, 0.03));
    CHECK (p.engine.getNumActiveOscillators (p.settings) == 3);

    // DETUNE is squared: at 50%, the outer copies are 25 cents away.
    auto half = unisonSine (2, 1.0f, 0.5f);
    half.on (69);
    const auto twoCopies = half.render (1.0);
    CHECK_THAT (levelAt (twoCopies[0], 440.0 * std::pow (2.0, 25.0 / 1200.0), 0, 48000), WithinAbs (1.0, 0.05));

    // 7 copies sit evenly either side: +-1/3, +-2/3 and +-1 of the detune.
    CHECK (synth::Engine::unisonPosition (0, 7) == 0.0f);
    CHECK_THAT (synth::Engine::unisonPosition (1, 7), WithinAbs (1.0 / 3.0, 1.0e-6));
    CHECK_THAT (synth::Engine::unisonPosition (2, 7), WithinAbs (-1.0 / 3.0, 1.0e-6));
    CHECK_THAT (synth::Engine::unisonPosition (6, 7), WithinAbs (-1.0, 1.0e-6));
    CHECK_THAT (synth::Engine::unisonPosition (1, 2), WithinAbs (1.0, 1.0e-6));
}

TEST_CASE ("UNISON SPREAD places the copies across the stereo field", "[synth][unison]")
{
    auto p = unisonSine (3, 1.0f, 1.0f, 1.0f);   // the sharp copy fully right, the flat one fully left
    p.on (69);
    const auto out = p.render (1.0);
    CHECK (levelAt (out[0], noteHz (70), 0, 48000) < 0.02);   // the sharp copy isn't on the left
    CHECK (levelAt (out[1], noteHz (68), 0, 48000) < 0.02);   // the flat one isn't on the right
    CHECK (levelAt (out[1], noteHz (70), 0, 48000) > 0.95);
    CHECK (levelAt (out[0], noteHz (68), 0, 48000) > 0.95);

    auto mono = unisonSine (3, 1.0f, 1.0f, 0.0f);
    mono.on (69);
    const auto centred = mono.render (0.5);
    for (size_t i = 0; i < centred[0].size(); ++i)
        REQUIRE (centred[0][i] == centred[1][i]);
}

TEST_CASE ("UNISON OCTAVES stacks the extra copies up an octave", "[synth][unison]")
{
    auto p = unisonSine (3, 0.5f);
    p.osc().unisonOctaves = 1;   // copy 1 at the note, copy 2 an octave up
    p.on (69);
    const auto out = p.render (1.0);
    CHECK_THAT (levelAt (out[0], 880.0, 0, 48000), WithinAbs (0.5, 0.03));
    CHECK (levelAt (out[0], 1760.0, 0, 48000) < 0.01);
}

TEST_CASE ("CHORDER: each extra note at its interval, level and pan, with its own unison copies", "[synth][chorder]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.osc().chord[0] = { true, 7.0f, 0.0f, 1.0f, 0.5f };   // a fifth up, fully right, half level
    p.osc().chord[1] = { true, -12.0f, 0.0f, 0.0f, 1.0f }; // an octave down
    p.on (69);
    auto out = p.render (1.0);
    CHECK_THAT (levelAt (out[1], noteHz (76), 0, 48000), WithinAbs (0.5, 0.02));
    CHECK (levelAt (out[0], noteHz (76), 0, 48000) < 0.01);
    CHECK_THAT (levelAt (out[0], 220.0, 0, 48000), WithinAbs (1.0, 0.02));
    CHECK (p.engine.getNumActiveOscillators (p.settings) == 3);

    // With unison, each chord note gets its copies too: (key + 2 chord notes) x 3 copies.
    p.osc().unisonVoices = 3;
    p.osc().unisonMix = 1.0f;
    p.osc().unisonDetune = 1.0f;
    p.off (69);
    p.render (0.5);
    p.on (69);
    out = p.render (1.0);
    CHECK (p.engine.getNumActiveOscillators (p.settings) == 9);
    CHECK_THAT (levelAt (out[1], noteHz (77), 0, 48000), WithinAbs (0.5, 0.03));   // the fifth's sharp copy
    CHECK_THAT (levelAt (out[0], noteHz (56), 0, 48000), WithinAbs (1.0, 0.03));   // the octave down's flat copy
}

TEST_CASE ("VIBRATO: the pitch wobbles by AMOUNT at RATE, and FADE IN grows it", "[synth][vibrato]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.osc().vibratoCents = 50.0f;
    p.osc().vibratoRateHz = 4.0f;
    p.on (69);
    const auto track = pitchTrack (p.render (2.0)[0], 10.0);
    const auto [low, high] = std::minmax_element (track.begin() + 10, track.end());
    CHECK_THAT (cents (440.0, *high), WithinAbs (50.0, 4.0));
    CHECK_THAT (cents (440.0, *low), WithinAbs (-50.0, 4.0));
    // 4 wobbles a second: 8 crossings of the note's own pitch in the last 2 seconds' worth, counted over 1.5 s here.
    int crossings = 0;
    for (size_t i = 51; i < track.size(); ++i)
        crossings += (track[i - 1] < 440.0) != (track[i] < 440.0) ? 1 : 0;
    CHECK (crossings >= 11);
    CHECK (crossings <= 13);

    Player fading;
    fading.osc().wave = synth::OscWave::sine;
    fading.osc().vibratoCents = 50.0f;
    fading.osc().vibratoRateHz = 4.0f;
    fading.osc().vibratoFadeMs = 1000.0f;
    fading.on (69);
    const auto grows = pitchTrack (fading.render (2.0)[0], 10.0);
    auto earlyMost = 0.0, lateMost = 0.0;
    for (size_t i = 1; i < 30; ++i)
        earlyMost = std::max (earlyMost, std::abs (cents (440.0, grows[i])));
    for (size_t i = 120; i < grows.size(); ++i)
        lateMost = std::max (lateMost, std::abs (cents (440.0, grows[i])));
    CHECK (earlyMost < 15.0);
    CHECK (lateMost > 40.0);
}

TEST_CASE ("VIBRATO INDEPENDENT: each copy wobbles on its own; GLOBAL: together", "[synth][vibrato]")
{
    // The note on the left only, its one unison copy in the middle: the right
    // side is the copy alone, and left minus right the note alone.
    const auto difference = [] (synth::VibratoMode mode)
    {
        auto p = unisonSine (2, 1.0f, 0.0f, 1.0f);
        p.osc().pan = -1.0f;
        p.osc().vibratoCents = 40.0f;
        p.osc().vibratoRateHz = 5.0f;
        p.osc().vibratoMode = mode;
        p.on (69);
        const auto out = p.render (2.0);
        std::vector<float> note (out[0].size());
        for (size_t i = 0; i < note.size(); ++i)
            note[i] = out[0][i] - out[1][i];
        const auto noteTrack = pitchTrack (note, 10.0), copyTrack = pitchTrack (out[1], 10.0);
        auto most = 0.0;
        for (size_t i = 5; i < noteTrack.size(); ++i)
            most = std::max (most, std::abs (cents (noteTrack[i], copyTrack[i])));
        return most;
    };
    CHECK (difference (synth::VibratoMode::global) < 2.0);
    CHECK (difference (synth::VibratoMode::independent) > 20.0);
}

TEST_CASE ("CPU: a 7-copy supersaw chord, and the worst case of unison and chorder together", "[synth][performance][unison]")
{
    const auto percent = [] (int oscillators, int chordNotes)
    {
        Player p;
        for (int i = 0; i < oscillators; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
            o.unisonSpread = 0.8f;
            o.vibratoCents = 5.0f;
            o.vibratoMode = synth::VibratoMode::independent;
            for (int k = 0; k < chordNotes; ++k)
                o.chord[(size_t) k].on = true;
        }
        p.settings.filter.on = true;
        p.settings.filter.type = synth::FilterType::lp24;
        p.settings.filter.cutoffHz = 3000.0f;
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto copies = p.engine.getNumActiveOscillators (p.settings);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (5.0);
        const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / 5000.0 * 100.0;
        WARN (oscillators << " oscillator(s), " << chordNotes << " chord notes, 8 keys: " << copies << " copies, " << cost << "% of one core");
        return cost;
    };
    CHECK (percent (1, 0) < 25.0);    // a supersaw chord: 56 copies
    CHECK (percent (4, 2) < 60.0);    // 4 oscillators x 3 notes x 7 copies x 8 keys: 672 copies (about 17% here; generous for slow CI machines)
}

TEST_CASE ("An engine is small enough to make on the stack (Windows gives a thread 1 MB)", "[synth]")
{
    // The voice pool (about 1 MB) lives on the heap; three or four engines on
    // the stack at once, as some tests make, once overflowed Windows' stack.
    CHECK (sizeof (synth::Engine) < 64 * 1024);
}
