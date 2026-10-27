// The SHAPER (stage 3e): each model's curve and harmonics, the anti-aliasing,
// its EQ bands and SPLIT, routing, place, velocity, mix, output; measured.
#include "SynthTestUtils.h"

#include "PluginProcessor.h"
#include "dsp/synth/Shaper.h"
#include "ui/SynthDisplays.h"

using Catch::Matchers::WithinRel;

namespace
{
    using synth::ShaperModel;

    const std::array<ShaperModel, 10> allModels { ShaperModel::soft, ShaperModel::hard, ShaperModel::tube, ShaperModel::diode,
                                                  ShaperModel::smooth, ShaperModel::cubic, ShaperModel::sineFold,
                                                  ShaperModel::triangleFold, ShaperModel::rectify, ShaperModel::halfRectify };

    // A sine of `hz` through one channel of a shaper (no EQ), with or without the anti-aliasing.
    std::vector<float> shapeSine (ShaperModel model, float drive, double hz, int samples, bool antiAliased = true, float amplitude = 1.0f)
    {
        synth::shaper::Channel channel;
        const auto gain = synth::shaper::driveGain (drive);
        const auto makeUp = synth::shaper::makeUp (model, gain);
        std::vector<float> out ((size_t) samples);
        for (int i = 0; i < samples; ++i)
        {
            const auto x = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
            out[(size_t) i] = antiAliased ? channel.distort (x, model, gain) * makeUp
                                          : (float) synth::shaper::curve (model, (double) x * gain) * makeUp;
        }
        return out;
    }

    // A sine on oscillator 1 (and whatever `setup` adds), as the engine plays it.
    std::array<std::vector<float>, 2> play (int note, double seconds, const std::function<void (Player&)>& setup, float velocity = 1.0f)
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        setup (p);
        p.on (note, velocity);
        return p.render (seconds);
    }

    void shaperOn (Player& p, int k, ShaperModel model, float drive)
    {
        auto& sh = p.settings.shapers[(size_t) k];
        sh.on = true;
        sh.model = model;
        sh.drive = drive;
    }
}

TEST_CASE ("SHAPER models: each curve's antiderivative matches it, and a full-scale sine comes out near full scale", "[shaper]")
{
    for (const auto model : allModels)
    {
        INFO (synth::shaperModelNames[(int) model]);
        // The antiderivative's slope is the curve (what the anti-aliasing relies on).
        for (const auto x : { -5.3, -2.0, -1.0, -0.6, -0.01, 0.0, 0.3, 0.999, 1.7, 3.2, 7.5 })
        {
            const auto h = 1.0e-5;
            const auto slope = (synth::shaper::antiderivative (model, x + h) - synth::shaper::antiderivative (model, x - h)) / (2.0 * h);
            CHECK_THAT (slope, WithinAbs (synth::shaper::curve (model, x), 1.0e-4));
        }
        // Silence in, silence out.
        CHECK_THAT (synth::shaper::curve (model, 0.0), WithinAbs (0.0, 1.0e-12));

        // DRIVE changes the grit, not the loudness: a full-scale sine peaks between 0.8 and 1.12 at any DRIVE
        // (a little over 1 where a clipped, square-ish wave rings once its top harmonics are taken away).
        for (const auto drive : { 0.0f, 0.5f, 1.0f })
        {
            INFO ("DRIVE " << drive);
            const auto out = shapeSine (model, drive, 200.0, 4800);
            auto peak = 0.0f;
            for (const auto v : out)
                peak = juce::jmax (peak, std::abs (v));
            CHECK (peak > 0.8f);
            CHECK (peak < 1.12f);
        }
    }
}

TEST_CASE ("SHAPER models' harmonics: SOFT odd only, TUBE and DIODE add even ones, RECTIFY an octave up", "[shaper]")
{
    const auto harmonics = [] (ShaperModel model, float drive)
    {
        const auto out = shapeSine (model, drive, 200.0, 9600);
        std::array<double, 5> h {};
        for (int k = 1; k <= 5; ++k)
            h[(size_t) k - 1] = levelAt (out, 200.0 * k, 4800, 4800);
        return h;
    };
    const auto soft = harmonics (ShaperModel::soft, 0.4f);
    CHECK (soft[2] > 0.05 * soft[0]);       // a strong 3rd
    CHECK (soft[1] < 1.0e-4 * soft[0]);     // no 2nd: symmetric curves make odd harmonics only
    CHECK (soft[3] < 1.0e-4 * soft[0]);

    for (const auto model : { ShaperModel::tube, ShaperModel::diode, ShaperModel::halfRectify })
    {
        INFO (synth::shaperModelNames[(int) model]);
        const auto h = harmonics (model, 0.4f);
        CHECK (h[1] > 0.03 * h[0]);         // a 2nd: lopsided curves add even harmonics
    }

    // RECTIFY folds the bottom half up: the note's own pitch is gone, the octave above is the loudest.
    const auto rectified = harmonics (ShaperModel::rectify, 0.3f);
    CHECK (rectified[0] < 1.0e-3);
    CHECK (rectified[1] > 0.15);
    for (const auto other : { rectified[2], rectified[3], rectified[4] })
        CHECK (rectified[1] > other);

    // More DRIVE, more distortion (SOFT's 3rd against the note).
    double last = 0.0;
    for (const auto drive : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto h = harmonics (ShaperModel::soft, drive);
        const auto third = h[2] / h[0];
        CHECK (third > last);
        last = third;
    }
}

TEST_CASE ("SHAPER anti-aliasing: far less folded-back noise than a plain distortion", "[shaper][aliasing]")
{
    // A plain distortion's harmonics fold back all over the spectrum; the
    // oversampled, anti-aliased one keeps them out. Reported for every model,
    // pitch and drive; checked where it should hold.
    for (const auto model : allModels)
        for (const auto drive : { 0.4f, 0.7f })
            for (const auto hz : { 880.0, 1760.0 })
            {
                const auto plain = Spectrum (shapeSine (model, drive, hz, 80000, false), 8000).loudestBetweenHarmonics (hz, 20000.0, 40.0);
                const auto shaped = Spectrum (shapeSine (model, drive, hz, 80000, true), 8000).loudestBetweenHarmonics (hz, 20000.0, 40.0);
                WARN (synth::shaperModelNames[(int) model] << " at " << hz << " Hz, +" << drive * synth::shaper::maxDriveDb
                      << " dB drive: loudest between harmonics " << shaped << " dB (a plain distortion: " << plain << " dB)");
                INFO (synth::shaperModelNames[(int) model] << " " << hz << " Hz, drive " << drive);
                CHECK ((shaped < plain - 15.0 || shaped < -100.0));   // (or nothing folds back at all)
                // The folders, driven hard, make more harmonics than 2x can hold: only the improvement is checked for them.
                if (model != ShaperModel::sineFold && model != ShaperModel::triangleFold)
                    CHECK (shaped < -45.0);
            }
}

TEST_CASE ("SHAPER oversampling: flat to 18 kHz but for the anti-aliasing's gentle top, and exactly its stated delay", "[shaper]")
{
    // HARD at no drive passes anything under full scale unchanged, so this
    // hears the oversampling and the anti-aliasing alone. The anti-aliasing
    // averages each sample with the one before (at twice the rate), which
    // rolls the top off by cos (pi f / 96 kHz): -0.5 dB at 10 kHz, -1.6 dB
    // at 18 kHz. The half-band filters add nothing to that (flat to 18 kHz).
    for (const auto hz : { 100.0, 1000.0, 10000.0, 18000.0 })
    {
        const auto out = shapeSine (ShaperModel::hard, 0.0f, hz, 24000, true, 0.5f);
        const auto averaging = db (std::cos (juce::MathConstants<double>::pi * hz / (2.0 * sampleRate)));
        INFO (hz << " Hz");
        CHECK_THAT (db (levelAt (out, hz, 4800, 19000) / 0.5), WithinAbs (averaging, 0.05));
    }
    // An impulse comes out centred `latency` samples later.
    synth::shaper::Channel channel;
    std::vector<float> out (64);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = channel.distort (i == 4 ? 0.5f : 0.0f, ShaperModel::hard, 1.0f);
    const auto peak = std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    CHECK (peak - out.begin() == 4 + synth::shaper::latency);
}

TEST_CASE ("SHAPER EQ: each band boosts or cuts where it's set, as its picture shows; 0 dB is no band", "[shaper]")
{
    for (const auto [gainDb, freq, q] : { std::tuple { 12.0f, 1000.0f, 1.0f }, std::tuple { -9.0f, 300.0f, 2.0f }, std::tuple { 6.0f, 6000.0f, 0.5f } })
    {
        const auto bell = synth::shaper::Bell::make (gainDb, freq, q, sampleRate);
        for (const auto hz : { 100.0, 300.0, 1000.0, 2000.0, 6000.0 })
        {
            synth::shaper::Channel channel;
            std::vector<float> out (19200);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = channel.eq ((float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate), bell, 0);
            const auto measured = db (levelAt (out, hz, 9600, 9600));
            INFO (gainDb << " dB at " << freq << " Hz, Q " << q << ": at " << hz << " Hz");
            CHECK_THAT (measured, WithinAbs (ShaperDisplay::bellDb (gainDb, freq, q, hz), 0.3));
        }
    }
    CHECK_THAT (ShaperDisplay::bellDb (12.0, 1000.0, 1.0, 1000.0), WithinAbs (12.0, 0.01));
    CHECK_FALSE (synth::shaper::Bell::make (0.0f, 1000.0f, 1.0f, sampleRate).active);
}

TEST_CASE ("SHAPER in a note: EQ SPLIT gives the left band 1 and the right band 2", "[shaper]")
{
    // A quiet A5 (880 Hz: barely distorted), band 1 +12 dB there, band 2 -12 dB there.
    const auto out = play (81, 0.5, [] (Player& p)
    {
        p.osc().volume = 0.05f;
        shaperOn (p, 0, ShaperModel::soft, 0.0f);
        auto& sh = p.settings.shapers[0];
        sh.eqGainDb = { 12.0f, -12.0f };
        sh.eqFreqHz = { 880.0f, 880.0f };
        sh.eqSplit = true;
    });
    const auto left = levelAt (out[0], 880.0, 12000, 9600), right = levelAt (out[1], 880.0, 12000, 9600);
    CHECK_THAT (db (left / right), WithinAbs (24.0, 1.0));
}

TEST_CASE ("SHAPER routing: only the oscillators ticked go through it", "[shaper]")
{
    // Oscillator 1 at A2 (110 Hz) through RECTIFY (its 110 Hz goes, an octave up comes);
    // oscillator 2 at 330 Hz, unticked, clean.
    const auto setup = []
    {
        return [] (Player& p)
        {
            auto& o2 = p.osc (1);
            o2.on = true;
            o2.wave = synth::OscWave::sine;
            o2.randomPhase = false;
            o2.transpose = 19.0f;
            o2.fineCents = 1.955f;   // exactly 3 x 110 Hz
            o2.toShaper = { false, true, true, true };
            shaperOn (p, 0, ShaperModel::rectify, 0.3f);
        };
    };
    const auto both = play (45, 0.5, setup());
    CHECK (levelAt (both[0], 110.0, 12000, 9600) < 1.0e-3);    // osc 1's own pitch: gone
    CHECK (levelAt (both[0], 220.0, 12000, 9600) > 0.05);      // its octave: there

    // Oscillator 2 comes out as it would on its own, clean.
    const auto clean = play (45, 0.5, [] (Player& p)
    {
        p.osc().on = false;
        auto& o2 = p.osc (1);
        o2.on = true;
        o2.wave = synth::OscWave::sine;
        o2.randomPhase = false;
        o2.transpose = 19.0f;
        o2.fineCents = 1.955f;
    });
    CHECK_THAT (levelAt (both[0], 330.0, 12000, 9600), WithinAbs (levelAt (clean[0], 330.0, 12000, 9600), 1.0e-4));
}

TEST_CASE ("SHAPER paths: oscillators on different routes don't distort each other", "[shaper]")
{
    // Oscillator 1 through a hard-driven shaper and the filter, oscillator 2
    // through the filter only: each has its own path, so playing both is
    // exactly playing each alone, added (one distortion would mix them).
    const auto setup = [] (bool first, bool second)
    {
        return [first, second] (Player& p)
        {
            p.osc().on = first;
            p.osc().wave = synth::OscWave::saw;
            auto& o2 = p.osc (1);
            o2.on = second;
            o2.randomPhase = false;
            o2.wave = synth::OscWave::saw;
            o2.transpose = 7.0f;
            o2.toShaper = { false, false, false, false };
            p.settings.filter.on = true;
            p.settings.filter.cutoffHz = 1500.0f;
            p.settings.filter.resonance = 0.5f;
            shaperOn (p, 0, ShaperModel::hard, 0.8f);
        };
    };
    const auto both = play (48, 0.4, setup (true, true));
    const auto first = play (48, 0.4, setup (true, false));
    const auto second = play (48, 0.4, setup (false, true));
    for (size_t i = 0; i < both[0].size(); i += 7)
        REQUIRE_THAT (both[0][i], WithinAbs (first[0][i] + second[0][i], 1.0e-5));
}

TEST_CASE ("SHAPER off: the sound is exactly as before, whatever the shapers are set to", "[shaper]")
{
    const auto setup = [] (bool configure)
    {
        return [configure] (Player& p)
        {
            p.osc().wave = synth::OscWave::saw;
            p.osc().unisonMix = 0.6f;
            p.osc (1).on = true;
            p.osc (1).toFilter = false;
            p.settings.filter.on = true;
            p.settings.filter.drive = 0.3f;
            if (configure)
                for (auto& sh : p.settings.shapers)
                {
                    sh.on = false;
                    sh.model = ShaperModel::sineFold;
                    sh.drive = 1.0f;
                    sh.eqGainDb = { 12.0f, -6.0f };
                    sh.afterFilter = true;
                }
        };
    };
    const auto plain = play (50, 0.3, setup (false));
    const auto configured = play (50, 0.3, setup (true));
    CHECK (plain[0] == configured[0]);
    CHECK (plain[1] == configured[1]);

    // MIX at 0: the sound coming in, sample for sample, as late as the
    // distorted sound would be (the oversampling's delay).
    const auto mixedOut = play (50, 0.3, [&] (Player& p) { setup (false) (p); shaperOn (p, 0, ShaperModel::hard, 1.0f); p.settings.shapers[0].mix = 0.0f; });
    const auto late = (size_t) synth::shaper::latency;
    // (Once the AMP's envelope has settled: it comes after the shaper, so during the attack it doesn't commute with a delay.)
    for (size_t i = 4800; i + late < plain[0].size(); i += 3)
        REQUIRE_THAT (mixedOut[0][i + late], WithinAbs (plain[0][i], 1.0e-6));
}

TEST_CASE ("SHAPER PLACE: before the filter its buzz is filtered away; after it, it isn't", "[shaper]")
{
    const auto highs = [] (bool after)
    {
        const auto out = play (45, 0.5, [after] (Player& p)
        {
            p.settings.filter.on = true;
            p.settings.filter.cutoffHz = 400.0f;
            shaperOn (p, 0, ShaperModel::hard, 0.8f);
            p.settings.shapers[0].afterFilter = after;
        });
        return levelAt (out[0], 110.0 * 25.0, 12000, 9600);   // the 25th harmonic, 2750 Hz
    };
    CHECK (db (highs (true)) > db (highs (false)) + 20.0);
}

TEST_CASE ("SHAPER DRIVE VEL: softer notes are driven less; full velocity gets DRIVE as set", "[shaper]")
{
    const auto thirdAgainstNote = [] (float velocity, float driveVelocity)
    {
        const auto out = play (57, 0.5, [driveVelocity] (Player& p)
        {
            p.settings.amp.velocity = 0.0f;   // the AMP ignores velocity: only the shaper hears it
            shaperOn (p, 0, ShaperModel::soft, 0.6f);
            p.settings.shapers[0].driveVelocity = driveVelocity;
        }, velocity);
        return levelAt (out[0], 660.0, 12000, 9600) / levelAt (out[0], 220.0, 12000, 9600);
    };
    CHECK_THAT (thirdAgainstNote (1.0f, 1.0f), WithinAbs (thirdAgainstNote (1.0f, 0.0f), 1.0e-4));
    CHECK (thirdAgainstNote (0.3f, 1.0f) < 0.65 * thirdAgainstNote (0.3f, 0.0f));
}

TEST_CASE ("SHAPER OUTPUT and MIX: -6 dB halves it; 50% MIX is half each", "[shaper]")
{
    const auto level = [] (float outputDb, float mix)
    {
        const auto out = play (57, 0.5, [=] (Player& p)
        {
            shaperOn (p, 0, ShaperModel::rectify, 0.3f);
            p.settings.shapers[0].outputDb = outputDb;
            p.settings.shapers[0].mix = mix;
        });
        return std::pair { levelAt (out[0], 220.0, 12000, 9600), levelAt (out[0], 440.0, 12000, 9600) };
    };
    const auto [dry, wet] = level (0.0f, 1.0f);
    CHECK (dry < 1.0e-3);   // RECTIFY: the note's pitch gone
    CHECK_THAT (level (-6.0206f, 1.0f).second, WithinRel (wet * 0.5, 0.01));
    const auto half = level (0.0f, 0.5f);
    CHECK_THAT (half.second, WithinRel (wet * 0.5, 0.02));      // half the distorted octave
    CHECK (half.first > 0.1);                                  // and the note back, from the clean half
}

TEST_CASE ("SHAPER: changing MODEL while a note plays doesn't blow up", "[shaper]")
{
    Player p;
    p.osc().wave = synth::OscWave::sine;
    p.settings.amp.envelope.sustain = 1.0f;
    shaperOn (p, 0, ShaperModel::soft, 0.5f);
    p.on (57);
    p.render (0.1);
    auto largestStep = 0.0f;
    for (const auto model : allModels)
    {
        p.settings.shapers[0].model = model;
        const auto out = p.render (0.05)[0];
        for (size_t i = 1; i < out.size(); ++i)
            largestStep = juce::jmax (largestStep, std::abs (out[i] - out[i - 1]));
    }
    // A new curve is a new wave from that sample on (RECTIFY flips the bottom
    // half at once), so the wave can jump by up to about twice its height; what
    // mustn't happen is the anti-aliasing blowing up (thousands).
    CHECK (largestStep < 2.5f);
}

TEST_CASE ("SHAPER in the plugin: its parameters reach the engine, and presets keep them", "[shaper][processor]")
{
    ParagonX1AudioProcessor processor;
    const auto set = [&] (const juce::String& id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    const auto defaults = processor.getSynthSettings();
    const synth::ShaperSettings fresh;
    for (const auto& sh : defaults.shapers)
    {
        CHECK_FALSE (sh.on);
        CHECK (sh.model == fresh.model);
        CHECK_THAT (sh.drive, WithinAbs (fresh.drive, 1.0e-6));
        CHECK_THAT (sh.eqFreqHz[1], WithinAbs (fresh.eqFreqHz[1], 0.01));
        CHECK_THAT (sh.eqQ[0], WithinAbs (fresh.eqQ[0], 1.0e-4));
    }
    for (const auto& o : defaults.oscillators)
        CHECK (o.toShaper == std::array<bool, 4> { true, true, true, true });

    set (ParamIDs::shp (2, "On"), 1.0f);
    set (ParamIDs::shp (2, "Model"), (float) ShaperModel::triangleFold);
    set (ParamIDs::shp (2, "Drive"), 0.75f);
    set (ParamIDs::shp (2, "Eq2Gain"), -7.5f);
    set (ParamIDs::shp (2, "EqSplit"), 1.0f);
    set (ParamIDs::shp (2, "Place"), 1.0f);
    set (ParamIDs::shp (2, "Output"), -3.0f);
    set (ParamIDs::osc (4, "Shp3"), 0.0f);
    const auto s = processor.getSynthSettings();
    const auto& sh = s.shapers[2];
    CHECK (sh.on);
    CHECK (sh.model == ShaperModel::triangleFold);
    CHECK_THAT (sh.drive, WithinAbs (0.75, 1.0e-4));
    CHECK_THAT (sh.eqGainDb[1], WithinAbs (-7.5, 0.06));
    CHECK (sh.eqSplit);
    CHECK (sh.afterFilter);
    CHECK_THAT (sh.outputDb, WithinAbs (-3.0, 0.06));
    CHECK_FALSE (s.oscillators[4].toShaper[2]);
    CHECK (s.oscillators[4].toShaper[1]);

    // Presets and projects keep them.
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK_THAT (rig->parameters.at (ParamIDs::shp (2, "Model")), WithinAbs ((float) ShaperModel::triangleFold, 1.0e-6));
    CHECK_THAT (rig->parameters.at (ParamIDs::osc (4, "Shp3")), WithinAbs (0.0, 1.0e-6));
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    ParagonX1AudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    const auto restored = other.getSynthSettings();   // (a copy: getSynthSettings makes a new one each time)
    const auto& copied = restored.shapers[2];
    CHECK (copied.on);
    CHECK (copied.model == ShaperModel::triangleFold);
    CHECK_FALSE (restored.oscillators[4].toShaper[2]);
}

TEST_CASE ("CPU: a supersaw chord through two shapers", "[shaper][performance]")
{
    // The fastest of three runs: a busy build machine only ever adds time, so
    // the fastest is the closest to what the sound itself costs.
    const auto once = [] (int shapers)
    {
        Player p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
        }
        p.settings.filter.on = true;
        if (shapers > 0)
        {
            shaperOn (p, 0, ShaperModel::tube, 0.5f);
            p.settings.shapers[0].eqGainDb = { 6.0f, -3.0f };
        }
        if (shapers > 1)
        {
            shaperOn (p, 1, ShaperModel::soft, 0.4f);
            p.settings.shapers[1].afterFilter = true;
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (2.0);
        return (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0;
    };
    const auto percent = [&once] (const char* what, int shapers)
    {
        const auto cost = juce::jmin (once (shapers), once (shapers), once (shapers));
        WARN (what << ": 2 saws x 7 copies x 8 keys through the filter, " << cost << "% of one core (the fastest of three)");
        return cost;
    };
    const auto plain = percent ("no shaper", 0);
    percent ("one shaper (TUBE, EQ)", 1);
    const auto two = percent ("two shapers (TUBE before, SOFT after)", 2);
    CHECK (two < plain + 15.0);
}
