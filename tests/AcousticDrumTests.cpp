// THE ACOUSTIC MODELS (PR7-D 0.24.0): modelled kick, snare and tom. What each part of the physics does to the sound,
// measured: the head's mode frequencies, a centre strike missing the modes with nodal diameters, the striker's hardness
// and the velocity brightening it, a hard hit's pitch glide, muffling killing the high modes first, the snare wires
// stopping before the head does, the kick's front head, VARIATION, the djent kit preset, the CPU.
#include "TestUtils.h"
#include <catch2/catch_approx.hpp>

#include "PluginProcessor.h"
#include "dsp/synth/Drums.h"
#include "rigs/FactoryRigs.h"

namespace
{
    using namespace synth::drums;

    SlotSettings acoustic (Model m)
    {
        SlotSettings s;
        s.model = m;
        s.variation = 0.0f;
        s.snap = 0.0f;   // no click or wires unless a test wants them: the head alone
        return s;
    }
    std::vector<float> hit (const SlotSettings& s, float velocity = 1.0f, double seconds = 2.0)
    {
        const auto b = Kit::renderHit (s, velocity, sampleRate, seconds);
        return { b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples() };
    }
    // Energy (dB) in a band over a stretch of time.
    double bandDb (const std::vector<float>& x, double from, double to, double lowHz, double highHz)
    {
        const auto start = (size_t) (from * sampleRate), length = (size_t) ((to - from) * sampleRate);
        juce::dsp::FFT fft (15);
        std::vector<float> data (1 << 16, 0.0f);
        for (size_t i = 0; i < length && i < (1u << 15) && start + i < x.size(); ++i)
            data[i] = x[start + i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) length));
        fft.performFrequencyOnlyForwardTransform (data.data());
        double sum = 1.0e-20;
        for (int bin = 1; bin < (1 << 14); ++bin)
            if (const auto hz = bin * sampleRate / (1 << 15); hz >= lowHz && hz < highHz)
                sum += (double) data[(size_t) bin] * data[(size_t) bin];
        return 10.0 * std::log10 (sum);
    }
    // The ideal membrane's (1,1) mode against its lowest, pulled in by the model's spacing for the snare (0.9).
    constexpr double snareFirstRatio = 1.0 + (3.8317 / 2.4048 - 1.0) * 0.9;
}

TEST_CASE ("ACOUSTIC: the snare's head rings at its modes: the lowest at 190 Hz (PITCH moves it), the next as the membrane's",
           "[acoustic]")
{
    auto s = acoustic (Model::acousticSnare);
    const auto x = hit (s);
    const Spectrum spectrum (x, 0);
    // Peaks at the lowest mode and at (1,1); little halfway between.
    const auto f0 = 190.0, f1 = 190.0 * snareFirstRatio;
    CHECK (spectrum.at (f0) > -12.0);
    CHECK (spectrum.at (f1) > -30.0);
    CHECK (spectrum.at (0.5 * (f0 + f1)) < spectrum.at (f1) - 15.0);
    // PITCH +12: everything an octave up.
    s.pitch = 12.0f;
    const Spectrum up (hit (s), 0);
    CHECK (up.at (2.0 * f0) > -12.0);
    CHECK (up.at (f0) < up.at (2.0 * f0) - 20.0);
}

TEST_CASE ("ACOUSTIC: struck at the centre, the modes with nodal diameters hardly sound; off-centre they do", "[acoustic]")
{
    auto s = acoustic (Model::acousticSnare);
    s.position = 0.0f;
    const Spectrum centre (hit (s), 0);
    s.position = 0.5f;
    const Spectrum offCentre (hit (s), 0);
    const auto f1 = 190.0 * snareFirstRatio;   // the (1,1) mode: a nodal line through the centre
    INFO ("(1,1) at the centre " << centre.at (f1) << " dB, off-centre " << offCentre.at (f1) << " dB");
    CHECK (offCentre.at (f1) > centre.at (f1) + 15.0);
}

TEST_CASE ("ACOUSTIC: a harder striker (TONE) and a harder hit (velocity) are brighter; a hard hit's pitch starts high and settles",
           "[acoustic]")
{
    // TONE: felt against hard plastic, on the tom: the highs in the first 20 ms against its body.
    auto s = acoustic (Model::acousticTom);
    s.tone = 0.0f;
    const auto soft = hit (s);
    s.tone = 1.0f;
    const auto hard = hit (s);
    const auto brightness = [] (const std::vector<float>& x) { return bandDb (x, 0.0, 0.02, 600.0, 8000.0) - bandDb (x, 0.0, 0.02, 50.0, 300.0); };
    INFO ("felt " << brightness (soft) << " dB, plastic " << brightness (hard) << " dB");
    CHECK (brightness (hard) > brightness (soft) + 10.0);

    // Velocity: a soft hit is darker against its body than a hard one (its contact is gentler).
    s.tone = 0.5f;
    const auto loud = hit (s, 1.0f), quiet = hit (s, 0.2f);
    CHECK (brightness (loud) > brightness (quiet) + 4.0);

    // The glide: the lowest mode's frequency over the first 30 ms against later, a hard hit and a soft one.
    s.tone = 0.0f;   // the head alone, its modes rolled off: the zero crossings follow the lowest
    s.decay = 0.6f;
    s.shell = 0.0f;
    const auto hardHit = hit (s, 1.0f, 2.0), softHit = hit (s, 0.15f, 2.0);
    const auto early = [] (const std::vector<float>& x) { return frequencyOf (x, (size_t) (0.005 * sampleRate), (size_t) (0.035 * sampleRate)); };
    // (The glide fades as the swing does, squared: by 1.2 s it's gone.)
    const auto late = [] (const std::vector<float>& x) { return frequencyOf (x, (size_t) (1.2 * sampleRate), (size_t) (1.8 * sampleRate)); };
    INFO ("hard " << early (hardHit) << " Hz -> " << late (hardHit) << " Hz; soft " << early (softHit) << " Hz -> " << late (softHit) << " Hz");
    CHECK (early (hardHit) > late (hardHit) * 1.04);
    CHECK (early (softHit) < late (softHit) * 1.02);
    CHECK (late (hardHit) == Catch::Approx (100.0).epsilon (0.05));
}

TEST_CASE ("ACOUSTIC: muffling (low DECAY) shortens it, the high modes most; the kick's front head (SHELL) rings on", "[acoustic]")
{
    // The high modes against the lowest, 150 ms in: more muffled, less of the highs left.
    auto s = acoustic (Model::acousticTom);
    s.position = 0.5f;
    s.decay = 0.9f;
    const auto open = hit (s);
    s.decay = 0.3f;
    const auto muffled = hit (s);
    const auto highs = [] (const std::vector<float>& x) { return bandDb (x, 0.15, 0.25, 180.0, 2000.0) - bandDb (x, 0.15, 0.25, 60.0, 130.0); };
    INFO ("open " << highs (open) << " dB, muffled " << highs (muffled) << " dB");
    CHECK (highs (muffled) < highs (open) - 6.0);
    CHECK (rms (muffled, (size_t) (0.4 * sampleRate), (size_t) (0.5 * sampleRate)) < rms (open, (size_t) (0.4 * sampleRate), (size_t) (0.5 * sampleRate)) * 0.45);

    // The kick: its front head (SHELL) keeps the lows going.
    auto k = acoustic (Model::acousticKick);
    k.shell = 0.0f;
    const auto dead = hit (k);
    k.shell = 1.0f;
    const auto ringing = hit (k);
    const auto tail = [] (const std::vector<float>& x) { return bandDb (x, 0.3, 0.6, 30.0, 120.0) - bandDb (x, 0.0, 0.05, 30.0, 120.0); };
    INFO ("no front head " << tail (dead) << " dB, front head " << tail (ringing) << " dB");
    CHECK (tail (ringing) > tail (dead) + 3.0);
}

TEST_CASE ("ACOUSTIC: the kick's beater click (SNAP) and the snare's wires (SNAP), which stop before the head does", "[acoustic]")
{
    auto k = acoustic (Model::acousticKick);
    k.tone = 0.9f;
    const auto noClick = hit (k);
    k.snap = 1.0f;
    const auto click = hit (k);
    CHECK (bandDb (click, 0.0, 0.005, 2000.0, 6000.0) > bandDb (noClick, 0.0, 0.005, 2000.0, 6000.0) + 15.0);

    auto s = acoustic (Model::acousticSnare);
    s.decay = 0.6f;
    const auto dry = hit (s);
    s.snap = 1.0f;
    const auto wires = hit (s);
    // The wires: noise above 4 kHz while the head swings.
    CHECK (bandDb (wires, 0.03, 0.08, 4000.0, 12000.0) > bandDb (dry, 0.03, 0.08, 4000.0, 12000.0) + 20.0);
    // Once the head's swing is under their tension they stop, while the head itself still rings (here, by when the
    // head is 40 dB down): no more wire noise than without them.
    const auto head = [] (const std::vector<float>& x, double t) { return bandDb (x, t, t + 0.05, 150.0, 260.0); };
    double t = 0.1;
    while (t < 1.8 && head (wires, t) > head (wires, 0.0) - 40.0)
        t += 0.05;
    INFO ("head 40 dB down at " << t << " s");
    REQUIRE (t < 1.8);
    CHECK (bandDb (wires, t, t + 0.05, 4000.0, 12000.0) < bandDb (dry, t, t + 0.05, 4000.0, 12000.0) + 3.0);
}

TEST_CASE ("ACOUSTIC: VARIATION makes each hit a little different; at 0 they're the same", "[acoustic]")
{
    const auto twoHits = [] (float variation)
    {
        Kit kit;
        kit.prepare (sampleRate, 256);
        kit.seedRandom (11);
        KitSettings settings;
        settings.on = true;
        auto s = acoustic (Model::acousticSnare);
        s.variation = variation;
        s.snap = 0.0f;
        s.decay = 0.1f;   // rung out well within the half second between the hits
        settings.slots[0] = s;
        std::vector<float> left ((size_t) (188 * 256)), right (left.size());   // about 1 s, whole blocks
        for (size_t done = 0; done < left.size(); done += 256)
        {
            juce::MidiBuffer midi;
            const auto at = (size_t) (0.5 * sampleRate);
            if (done == 0 || (at >= done && at < done + 256))
                midi.addEvent (juce::MidiMessage::noteOn (1, settings.rootKey, 1.0f), done == 0 ? 0 : (int) (at - done));
            Outputs out;
            out.left = left.data() + done;
            out.right = right.data() + done;
            kit.process (out, 256, midi, 0, settings);
        }
        // The second hit's first 0.3 s against the first's (the first has rung out by then: 0.5 s apart, short decay).
        double difference = 0.0, size = 0.0;
        for (size_t i = (size_t) (0.01 * sampleRate); i < (size_t) (0.3 * sampleRate); ++i)   // after the stick's click (noise)
        {
            const auto a = left[i], b = left[(size_t) (0.5 * sampleRate) + i];
            difference += (double) (a - b) * (a - b);
            size += (double) a * a;
        }
        return std::sqrt (difference / size);
    };
    const auto same = twoHits (0.0f), varied = twoHits (1.0f);
    INFO ("difference at 0: " << same << ", at 100%: " << varied);
    CHECK (same < 0.05);
    CHECK (varied > 0.2);
}

TEST_CASE ("ACOUSTIC: the Djent Kit preset is built of them; a kit of acoustic drums hit every sixteenth note stays light on the CPU",
           "[acoustic][performance]")
{
    const auto rigs = makeFactoryRigs();
    const auto djent = std::find_if (rigs.begin(), rigs.end(), [] (const Rig& r) { return r.name == "Djent Kit"; });
    REQUIRE (djent != rigs.end());
    CHECK (djent->parameters.at (ParamIDs::drum (0, "Model")) == (float) (int) Model::acousticKick);
    CHECK (djent->parameters.at (ParamIDs::drum (2, "Model")) == (float) (int) Model::acousticSnare);
    CHECK (djent->parameters.at (ParamIDs::drum (5, "Model")) == (float) (int) Model::acousticTom);
    for (const auto& pattern : djent->modulation.drumPatterns)
        CHECK (pattern.notes.size() >= 8);

    // Six acoustic drums (kick, two snares, three toms) hit on every sixteenth at 120 BPM, for 8 s.
    Kit kit;
    kit.prepare (sampleRate, 256);
    KitSettings settings;
    settings.on = true;
    for (const auto& [slot, model] : { std::pair { 0, Model::acousticKick }, std::pair { 2, Model::acousticSnare }, std::pair { 4, Model::acousticSnare },
                                       std::pair { 5, Model::acousticTom }, std::pair { 7, Model::acousticTom }, std::pair { 9, Model::acousticTom } })
    {
        auto s = acoustic (model);
        s.snap = 0.6f;
        s.decay = 0.6f;
        settings.slots[(size_t) slot] = s;
    }
    std::vector<float> left (256), right (256);
    const auto blocks = (int) (8.0 * sampleRate / 256);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b % (int) (0.125 * sampleRate / 256) == 0)
            for (const auto slot : { 0, 2, 4, 5, 7, 9 })
                midi.addEvent (juce::MidiMessage::noteOn (1, settings.rootKey + slot, 1.0f), 0);
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        Outputs out;
        out.left = left.data();
        out.right = right.data();
        kit.process (out, 256, midi, 0, settings);
    }
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 8000.0 * 100.0;
    WARN ("6 acoustic drums every sixteenth note at 120 BPM: " << percent << "% of one core");
    CHECK (percent < 14.0);
}
