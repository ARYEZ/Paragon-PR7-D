// THE INSERT BUSES (stage 7d): oscillators routed to buses A to D, each bus's DRIVE, GLITCH, MOD and LO-FI working on
// just those oscillators, before the sends (the bus's own sends feeding DELAY and REVERB), measured from what the
// plugin plays. (PR7-G's own DRIVE and GLITCH tests: DriveGlitchTests.cpp.)
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"
#include "ui/FxPage.h"
#include "ui/MainView.h"

namespace
{
    using Model = effects::Drive::Model;

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.seedRandomForTest (1);
            set (ParamIDs::osc (0, "Wave"), (float) (int) synth::OscWave::sine);
            set (ParamIDs::ampSustain, 1.0f);
            set (ParamIDs::ampRelease, 5.0f);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        void addOscillator (int index, float transpose)
        {
            set (ParamIDs::osc (index, "On"), 1.0f);
            set (ParamIDs::osc (index, "Wave"), (float) (int) synth::OscWave::sine);
            set (ParamIDs::osc (index, "Transpose"), transpose);
        }
        void driveOn (int bus, Model model, float drive)
        {
            set (ParamIDs::ins (bus, "DriveOn"), 1.0f);
            set (ParamIDs::ins (bus, "DriveModel"), (float) (int) model);
            set (ParamIDs::ins (bus, "DriveGain"), drive);
        }
        std::array<std::vector<float>, 2> play (int note, double held, double seconds)
        {
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            processor.getKeyboardState().noteOn (1, note, 0.8f);
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                if (b == (int) (held * sampleRate) / blockSize)
                    processor.getKeyboardState().noteOff (1, note, 0.0f);
                juce::MidiBuffer midi;
                buffer.clear();
                processor.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
    };

    double tone (const std::vector<float>& x, double hz, double from, double length = 0.1)
    {
        return levelAt (x, hz, (size_t) (from * sampleRate), (size_t) (length * sampleRate));
    }
    double peakOf (const std::vector<float>& x, double from = 0.0)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }
    // A frequency's level over [from, from + length) seconds, through a Hann window: next to a loud note a few
    // tens of Hz away, a plain (rectangular) measurement leaks far more than these tests look for.
    double windowed (const std::vector<float>& x, double hz, double from, double length = 0.25)
    {
        const auto start = (size_t) (from * sampleRate), n = (size_t) (length * sampleRate);
        double re = 0.0, im = 0.0, weights = 0.0;
        for (size_t i = 0; i < n && start + i < x.size(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) n);
            const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
            re += w * x[start + i] * std::cos (phase);
            im += w * x[start + i] * std::sin (phase);
            weights += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / weights;
    }
    std::vector<float> minus (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> d (std::min (a.size(), b.size()));
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = a[i] - b[i];
        return d;
    }
    // Oscillator 2's pitch in these tests: a tritone and 17 cents up from A3 (314.2 Hz), so no harmonic of either note,
    // and no simple mixture of them, lands near the other's.
    constexpr float otherTranspose = 6.0f, otherFine = 17.0f;
    const double otherHz = 220.0 * std::pow (2.0, (6.0 + 0.17) / 12.0);

    float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        float result = 0.0f;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("INSERTS: with no oscillator on a bus, its effects change nothing, sample for sample", "[inserts]")
{
    const auto run = [] (bool busesSet)
    {
        Plugin p;
        p.set (ParamIDs::osc (0, "Wave"), (float) (int) synth::OscWave::saw);
        if (busesSet)
            for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
            {
                p.driveOn (b, Model::rabid, 9.0f);
                p.set (ParamIDs::ins (b, "GlitchOn"), 1.0f);
                p.set (ParamIDs::insModIds (b).on, 1.0f);
                p.set (ParamIDs::ins (b, "LofiOn"), 1.0f);
            }
        return p.play (57, 0.3, 0.5);
    };
    const auto plain = run (false), set = run (true);
    CHECK (plain[0] == set[0]);
    CHECK (plain[1] == set[1]);
}

TEST_CASE ("INSERTS: on a bus with its effects off, an oscillator sounds as it did", "[inserts]")
{
    const auto run = [] (bool onBus)
    {
        Plugin p;
        p.set (ParamIDs::osc (0, "Wave"), (float) (int) synth::OscWave::saw);
        if (onBus)
            p.set (ParamIDs::osc (0, "Insert"), 2.0f);   // B
        return p.play (57, 0.3, 0.5);
    };
    const auto plain = run (false), bused = run (true);
    REQUIRE (peakOf (plain[0]) > 0.05);
    CHECK (maxDifference (plain[0], bused[0]) < 1.0e-6f);
    CHECK (maxDifference (plain[1], bused[1]) < 1.0e-6f);
}

TEST_CASE ("INSERTS: a bus's DRIVE works on its oscillators only", "[inserts]")
{
    // A3 (220 Hz) on oscillator 1, on bus A with BANSHEE driven hard; oscillator 2 (314.2 Hz) not on a bus. What the
    // drive changes (driven minus clean) holds A3's odd harmonics (660 Hz), and nothing of oscillator 2: not its
    // note, not its 3rd harmonic, not its mixtures with A3 that clipping them together would make.
    const auto run = [] (bool driven)
    {
        Plugin p;
        p.addOscillator (1, otherTranspose);
        p.set (ParamIDs::osc (1, "Fine"), otherFine);
        p.set (ParamIDs::osc (0, "Insert"), 1.0f);
        if (driven)
            p.driveOn (0, Model::screamer, 9.0f);
        return p.play (57, 0.7, 0.7)[0];
    };
    const auto clean = run (false), driven = run (true);
    const auto change = minus (driven, clean);
    REQUIRE (windowed (clean, otherHz, 0.3) > 0.1);
    CHECK (windowed (change, 660.0, 0.3) > 0.01);                          // A3's 3rd harmonic, from the drive
    CHECK (windowed (clean, 660.0, 0.3) < 1.0e-4);
    CHECK (windowed (change, otherHz, 0.3) < 1.0e-4);                      // oscillator 2 isn't touched
    CHECK (windowed (change, 3.0 * otherHz, 0.3) < 1.0e-4);
    CHECK (windowed (change, 2.0 * otherHz - 220.0, 0.3) < 1.0e-4);        // nor mixed with A3 in the clipping
    CHECK (windowed (change, otherHz + 220.0, 0.3) < 1.0e-4);
}

TEST_CASE ("INSERTS: a bus sends at its own amounts, after its effects; its oscillators' own sends aren't used", "[inserts][sends]")
{
    // A delay of plain 200 ms echoes at full MIX. Oscillator 1 on bus A (driven), its own DELAY send at 0.
    const auto echoes = [] (float busSend)
    {
        Plugin p;
        p.set (ParamIDs::delayOn, 1.0f);
        p.set (ParamIDs::delaySync, 0.0f);
        p.set (ParamIDs::delayTime, 200.0f);
        p.set (ParamIDs::delayFeedback, 0.0f);
        p.set (ParamIDs::delayMix, 1.0f);
        p.set (ParamIDs::osc (0, "SendDelay"), 0.0f);
        p.set (ParamIDs::osc (0, "Insert"), 1.0f);
        p.driveOn (0, Model::screamer, 9.0f);
        p.set (ParamIDs::ins (0, "SendDelay"), busSend);
        return p.play (57, 0.1, 0.5)[0];
    };
    const auto sent = echoes (1.0f), dry = echoes (0.0f), half = echoes (0.5f);
    // The echo (200-300 ms): there at the bus's 100%, with the drive's harmonic in it; none at 0%; 6 dB down at 50%.
    CHECK (tone (sent, 220.0, 0.21, 0.08) > 0.05);
    CHECK (tone (sent, 660.0, 0.21, 0.08) > 0.005);
    CHECK (tone (dry, 220.0, 0.21, 0.08) < 1.0e-4);
    CHECK_THAT (db (tone (half, 220.0, 0.21, 0.08) / tone (sent, 220.0, 0.21, 0.08)), WithinAbs (-6.0, 0.5));
}

TEST_CASE ("INSERTS: a sound on the left stays on the left through every effect on a bus", "[inserts]")
{
    Plugin p;
    p.set (ParamIDs::osc (0, "Pan"), -1.0f);
    p.set (ParamIDs::osc (0, "Insert"), 3.0f);   // C
    p.driveOn (2, Model::rabid, 7.0f);
    p.set (ParamIDs::ins (2, "GlitchOn"), 1.0f);
    p.set (ParamIDs::ins (2, "GlitchMode"), (float) (int) effects::Glitch::Mode::interfere);
    p.set (ParamIDs::insModIds (2).on, 1.0f);
    p.set (ParamIDs::insModIds (2).type, (float) (int) effects::Modulation::Type::flanger);
    p.set (ParamIDs::ins (2, "LofiOn"), 1.0f);
    p.set (ParamIDs::ins (2, "LofiNoise"), 0.0f);
    const auto out = p.play (57, 0.4, 0.6);
    CHECK (rms (out[0], (size_t) (0.05 * sampleRate), out[0].size()) > 0.01);
    CHECK (peakOf (out[1]) == 0.0);
}

TEST_CASE ("INSERTS: each bus is its own: a drive on A leaves B's oscillator clean", "[inserts]")
{
    Plugin p;
    p.addOscillator (1, otherTranspose);
    p.set (ParamIDs::osc (1, "Fine"), otherFine);
    p.set (ParamIDs::osc (0, "Insert"), 1.0f);   // A3 on A, driven
    p.set (ParamIDs::osc (1, "Insert"), 2.0f);   // the other on B, nothing on
    p.driveOn (0, Model::screamer, 9.0f);
    const auto out = p.play (57, 0.7, 0.7)[0];
    CHECK (windowed (out, 660.0, 0.3) > 0.01);
    CHECK (windowed (out, 3.0 * otherHz, 0.3) < 1.0e-4);
    CHECK (windowed (out, 2.0 * otherHz - 220.0, 0.3) < 1.0e-4);
    CHECK (p.processor.getInsertPeakDb (0) > -40.0f);
    CHECK (p.processor.getInsertPeakDb (2) < -99.0f);   // nothing on C
}

TEST_CASE ("INSERTS: CPU, a chord on a bus through RABID (one a side, 8x oversampled) and GLITCH", "[inserts][performance]")
{
    const auto cpu = [] (int how)   // 0: no bus; 1: on a bus with its effects on; 2: the effects on, nothing routed
    {
        auto best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            Plugin p;
            p.set (ParamIDs::osc (0, "Wave"), (float) (int) synth::OscWave::saw);
            if (how > 0)
            {
                p.driveOn (0, Model::rabid, 7.0f);
                p.set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            }
            if (how == 1)
                p.set (ParamIDs::osc (0, "Insert"), 1.0f);
            for (const auto note : { 48, 55, 60, 64 })
                p.processor.getKeyboardState().noteOn (1, note, 0.8f);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            const auto started = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < (int) (4.0 * sampleRate) / blockSize; ++b)
                p.processor.processBlock (buffer, midi);
            best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 4000.0 * 100.0);
        }
        return best;
    };
    const auto plain = cpu (0), bused = cpu (1), unrouted = cpu (2);
    WARN ("a 4-note saw chord: no bus " << plain << "% of one core; on bus A with RABID and GLITCH " << bused
          << "%; those on with nothing routed " << unrouted << "%");
    CHECK (bused < plain + 45.0);       // RABID solves its circuit at 8x, once a side (generous for CI machines)
    CHECK (unrouted < plain + 1.0);     // a bus nothing plays into costs nothing
}

TEST_CASE ("The FX page's INSERTS view: the bus strip, each bus's units and pages", "[inserts][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::fx);
    auto& page = view->getFxPage();
    page.getInsertsTab().onClick();
    CHECK (page.getView() == FxPage::View::inserts);
    auto& strip = page.getInsertStrip();
    std::vector<juce::Component*> shown { &page.getInsertsTab(), &page.getSendsTab(), &page.getModFxTab(), &page.getMasterTab(), &strip };
    const juce::String names[] = { "DRIVE", "GLITCH", "MOD", "LO-FI" };
    for (int i = 0; i < 4; ++i)
    {
        CHECK (page.getInsertUnit (0, i).getSpec().name == names[i]);
        shown.push_back (&page.getInsertUnit (0, i));
    }
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (shown[i]->isVisible());
        CHECK (page.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
    }
    CHECK_FALSE (page.getInsertUnit (1, 0).isVisible());

    // Routing from the strip: oscillator 3's button puts it on the bus shown, again takes it off.
    auto& insert = *processor.getParameters().getParameter (ParamIDs::osc (2, "Insert"));
    strip.getOscillatorButton (2).onClick();
    CHECK (juce::roundToInt (insert.convertFrom0to1 (insert.getValue())) == 1);
    strip.getBusButton (2).onClick();   // bus C: its units, its sends
    CHECK (strip.getBus() == 2);
    CHECK (page.getInsertUnit (2, 0).isVisible());
    CHECK_FALSE (page.getInsertUnit (0, 0).isVisible());
    CHECK (strip.getSend (0).getParameterId() == ParamIDs::ins (2, "SendDelay"));
    CHECK_FALSE (strip.getOscillatorButton (2).getToggleState());   // it's on A, not C
    strip.getOscillatorButton (2).onClick();
    CHECK (juce::roundToInt (insert.convertFrom0to1 (insert.getValue())) == 3);

    // POWER is each effect's ON on that bus; EDIT opens its page; BACK TO RACK returns.
    auto& driveOn = *processor.getParameters().getParameter (ParamIDs::ins (2, "DriveOn"));
    page.getInsertUnit (2, 0).getPower().toggle();
    CHECK (driveOn.getValue() > 0.5f);
    const FxPage::View views[] = { FxPage::View::insertDrive, FxPage::View::insertGlitch, FxPage::View::insertMod, FxPage::View::insertLoFi };
    for (int i = 0; i < 4; ++i)
    {
        page.getInsertUnit (2, i).getEdit().onClick();
        CHECK (page.getView() == views[i]);
        juce::Component& shownPage = i < 2 ? static_cast<juce::Component&> (page.getInsertEffectPage (2, i))
                                           : static_cast<juce::Component&> (page.getInsertModFxPage (2, i - 2));
        CHECK (shownPage.isVisible());
        if (i < 2)
        {
            auto& effectPage = page.getInsertEffectPage (2, i);
            for (auto* c : effectPage.getShownControls())
            {
                INFO (c->getParameterId());
                CHECK (c->getParameterId().startsWith ("ins3"));
                CHECK (effectPage.getLocalBounds().contains (c->getBounds()));
            }
            effectPage.onClose();
        }
        else
            page.getInsertModFxPage (2, i - 2).onClose();
        CHECK (page.getView() == FxPage::View::inserts);
    }

    // DRIVE's page shows what the model uses: ABYSS its EQ and no TONE; SKYLINE its ATTACK and GATE.
    auto set = [&] (const juce::String& id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto& drivePage = page.getInsertEffectPage (2, 0);
    const auto shownIds = [&]
    {
        drivePage.resized();
        juce::StringArray ids;
        for (auto* c : drivePage.getShownControls())
            ids.add (c->getParameterId());
        return ids;
    };
    set (ParamIDs::ins (2, "DriveModel"), (float) (int) Model::abyss);
    auto ids = shownIds();
    CHECK (ids.contains (ParamIDs::ins (2, "DriveMidFreq")));
    CHECK_FALSE (ids.contains (ParamIDs::ins (2, "DriveTone")));
    set (ParamIDs::ins (2, "DriveModel"), (float) (int) Model::skyline);
    ids = shownIds();
    CHECK (ids.contains (ParamIDs::ins (2, "DriveAttack")));
    CHECK (ids.contains (ParamIDs::ins (2, "DriveTone")));
    CHECK_FALSE (ids.contains (ParamIDs::ins (2, "DriveHigh")));
}

TEST_CASE ("INSERTS: DRIVE's page draws what the drive does", "[inserts][ui]")
{
    ParagonX1AudioProcessor processor;
    auto set = [&] (const juce::String& id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    DrivePreview preview (processor, 0);
    // BANSHEE clips both halves alike: odd harmonics, hardly any even; at DRIVE 0 the cycle out is nearly the sine in.
    set (ParamIDs::ins (0, "DriveModel"), (float) (int) Model::screamer);
    set (ParamIDs::ins (0, "DriveGain"), 10.0f);
    preview.render();
    const auto& h = preview.getHarmonicsDb();
    CHECK (h[2] > -40.0f);            // the 3rd
    CHECK (h[1] < h[2] - 20.0f);      // the 2nd far below
    // HALO clips one half harder: a strong 2nd.
    set (ParamIDs::ins (0, "DriveModel"), (float) (int) Model::halo);
    preview.render();
    CHECK (preview.getHarmonicsDb()[1] > -30.0f);
    CHECK (preview.getCycleOut().size() == 400);
}

TEST_CASE ("INSERTS: the factory preset Driven Lead: its lead through bus A's drive, the sub clean", "[inserts][rigs]")
{
    Plugin p;
    const auto* entry = p.processor.getRigLibrary().findFactory ("Driven Lead");
    REQUIRE (entry != nullptr);
    p.processor.loadRig (*entry);
    const auto s = p.processor.getSynthSettings();
    CHECK (s.oscillators[0].insertBus == 1);
    CHECK (s.oscillators[1].insertBus == 0);
    CHECK (p.processor.getInsertDriveSettings (0).on);
    const auto out = p.play (57, 0.5, 0.6);
    CHECK (rms (out[0], (size_t) (0.1 * sampleRate), (size_t) (0.5 * sampleRate)) > 0.02);
    CHECK (p.processor.getInsertPeakDb (0) > -60.0f);
}

TEST_CASE ("INSERTS: the DRIVE leaves no offset behind (RABID's charged capacitor is blocked)", "[inserts]")
{
    // A loud note through RABID, let go: 3 seconds later the bus is silent (on PR7-G the amp blocks RABID's offset).
    Plugin p;
    p.set (ParamIDs::osc (0, "Wave"), (float) (int) synth::OscWave::saw);
    p.set (ParamIDs::ampRelease, 50.0f);
    p.set (ParamIDs::osc (0, "Insert"), 1.0f);
    p.driveOn (0, Model::rabid, 8.0f);
    const auto out = p.play (45, 0.5, 4.0);
    CHECK (peakOf (out[0], 3.5) < 1.0e-4);
    CHECK (peakOf (out[1], 3.5) < 1.0e-4);
}

TEST_CASE ("INSERTS: CPU of each DRIVE model, a side and a bus (both sides)", "[inserts][performance]")
{
    // Reported, not limited: PR7-G's circuits, solved at 8x. A bus runs one a side. Two inputs: a C major saw chord
    // at about a played level (what DRIVE's page quotes) and loud white noise, the circuits' worst case (HALO's solver
    // works hardest on it).
    for (int m = 0; m < effects::Drive::modelNames.size(); ++m)
        for (const auto noise : { false, true })
        {
            auto best = 1.0e9;
            for (int run = 0; run < 2; ++run)
            {
                auto drive = std::make_unique<effects::Drive>();
                drive->prepare (sampleRate, blockSize);
                effects::Drive::Settings s;
                s.on = true;
                s.model = (Model) m;
                s.drive = 7.0f;
                std::vector<float> x ((size_t) blockSize);
                juce::Random random (2);
                double phases[4] {};
                constexpr double chord[] = { 130.81, 164.81, 196.0, 261.63 };
                const auto started = juce::Time::getMillisecondCounterHiRes();
                for (int b = 0; b < (int) (2.0 * sampleRate) / blockSize; ++b)
                {
                    for (auto& v : x)
                    {
                        if (noise)
                        {
                            v = 0.3f * (random.nextFloat() - 0.5f);
                            continue;
                        }
                        auto sum = 0.0;
                        for (size_t k = 0; k < 4; ++k)
                        {
                            phases[k] += chord[k] / sampleRate;
                            phases[k] -= std::floor (phases[k]);
                            sum += 2.0 * phases[k] - 1.0;
                        }
                        v = (float) (0.5 * sum / 4.0);
                    }
                    float* channels[] = { x.data() };
                    drive->process (juce::dsp::AudioBlock<float> (channels, 1, (size_t) blockSize), s);
                }
                best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0);
            }
            WARN (effects::Drive::modelNames[m] << (noise ? " (noise)" : " (chord)") << ": " << best
                                                << "% of one core a side, " << 2.0 * best << "% on a bus");
        }
}
