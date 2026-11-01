#pragma once

#include <juce_core/juce_core.h>

#include <array>

// THE MIXER'S MIC CHANNELS (PR7-D 0.20.0): a multi-mic kit's drums recorded through several microphones (close mics on
// each drum, overheads and room mics over the kit) play each mic's recording through its own channel, as a sampled
// drum kit's mixer does (inspired by the reference's mixer chapter; our own channels, names and design). Only drums
// from multi-mic kits use them; synth drums and single samples go their own way as before.
//
// The channels are fixed, so a kit's mics are sorted into them by name (our own rules, from the words the files and
// SFZ labels use: "kick in", "OH", "room"...). A mic a channel's own drum is close to is DIRECT sound there; another
// drum heard through it is BLEED (the snare in the kick mic). Overheads, room and ambience hear the whole kit, so
// nothing is bleed there.
namespace synth::mixer
{
    enum Channel { kickIn, kickOut, snareTop, snareBottom, hiHat, tom1, tom2, tom3, cymbals, overheads, room, ambience, other };
    inline constexpr int numChannels = 13;
    inline const juce::StringArray channelNames { "KICK IN", "KICK OUT", "SNARE TOP", "SNARE BTM", "HI-HAT", "TOM 1", "TOM 2", "TOM 3",
                                                  "CYMBALS", "OVERHEADS", "ROOM", "AMBIENCE", "OTHER" };

    inline bool hearsWholeKit (int channel) noexcept { return channel == overheads || channel == room || channel == ambience; }

    // A mic's channel from its name (a file's path or name, an SFZ label): -1 if nothing in it says.
    inline int channelForName (const juce::String& text)
    {
        // Lower case, separators as spaces, padded so whole words can be looked for.
        const auto t = " " + text.toLowerCase().replaceCharacters ("_-./\\()[]", "         ") + " ";
        const auto has = [&t] (std::initializer_list<const char*> words)
        {
            for (const auto* w : words)
                if (t.contains (juce::String (" ") + w + " "))
                    return true;
            return false;
        };
        const auto contains = [&t] (std::initializer_list<const char*> parts)
        {
            for (const auto* p : parts)
                if (t.contains (p))
                    return true;
            return false;
        };
        // The room mics before the close ones ("room kick" is a room mic); far / mono rooms as AMBIENCE.
        if (contains ({ "room far", "far room", "ambience", "ambient" }) || has ({ "amb", "amb2", "far", "mono room" }))
            return ambience;
        if (contains ({ "room" }) || has ({ "rm" }))
            return room;
        if (contains ({ "overhead" }) || has ({ "oh", "ohs", "ovh", "ohl", "ohr", "oh l", "oh r" }))
            return overheads;
        if (contains ({ "kick in", "kickin", "bd in", "bass drum in", "kick inside" }) || has ({ "kin", "bdin", "kik in" }))
            return kickIn;
        if (contains ({ "kick out", "kickout", "bd out", "kick sub", "subkick" }) || has ({ "kout", "bdout", "sub" }))
            return kickOut;
        if (contains ({ "snare bot", "snare btm", "snare under", "sn bot", "sd bot", "snarebot", "snarebtm", "snareunder", "sdbot", "snbot" })
            || has ({ "snb", "sdb", "bottom", "btm", "bot" }))
            return snareBottom;
        if (contains ({ "snare top", "sn top", "sd top", "snaretop", "sdtop", "sntop", "snare" }) || has ({ "snt", "sdt", "sd", "sn", "top" }))
            return snareTop;
        if (contains ({ "hihat", "hi hat", "hi-hat", "hat" }) || has ({ "hh" }))
            return hiHat;
        if (contains ({ "floor tom 2", "floor 2", "tom 3", "tom3" }) || has ({ "t3", "ft2" }))
            return tom3;
        if (contains ({ "floor tom", "floor", "tom 2", "tom2" }) || has ({ "t2", "ft", "ft1" }))
            return tom2;
        if (contains ({ "rack tom", "tom 1", "tom1", "tom" }) || has ({ "t1", "rt" }))
            return tom1;
        if (contains ({ "cymbal", "ride", "crash", "china", "splash" }) || has ({ "cym", "rd", "cr" }))
            return cymbals;
        if (contains ({ "kick" }) || has ({ "bd", "kik" }))
            return kickIn;
        return -1;
    }

    // Each channel's settings, as the kit plays them.
    struct ChannelSettings
    {
        float gain = 1.0f;             // the fader (linear)
        float pan = 0.0f;              // -1 to 1, a balance
        bool mute = false, solo = false, invert = false;
        float bleedGain = 1.0f;        // BLEED: every other drum heard through it, together
        bool bleedOn = true;
        double offsetSeconds = 0.0;    // TIME OFFSET: the channel later, up to 10 ms
        double releaseSeconds = 0.0;   // RELEASE: each hit faded after this long (0: as recorded)
        int output = 0;                // 0 MAIN, 1-15 OUT 2-16
        int route = 0;                 // 0.21.0: routeNames' index (0: its OUTPUT; else a bus)
    };

    // THE BUSES (0.21.0): channels for grouping (inspired by the reference's mixer chapter, our own design). A mic channel
    // or a bus goes to its OUTPUT or, by its ROUTE, into a bus; and SENDS a copy into a bus, an FX BUS (the FX page's
    // insert buses A-D, with their effects) or the DELAY or REVERB. Buses mix in order: a bus feeds only later buses, so
    // nothing loops back.
    inline constexpr int numBuses = 8;
    inline const juce::StringArray busNames { "BUS 1", "BUS 2", "BUS 3", "BUS 4", "BUS 5", "BUS 6", "BUS 7", "BUS 8" };
    // A channel's or bus's ROUTE, by index (saved: only append): 0 its OUTPUT, 1-8 BUS 1-8, 9-12 FX BUS A-D.
    inline const juce::StringArray routeNames { "Output", "Bus 1", "Bus 2", "Bus 3", "Bus 4", "Bus 5", "Bus 6", "Bus 7", "Bus 8",
                                                "FX Bus A", "FX Bus B", "FX Bus C", "FX Bus D" };
    inline constexpr int firstFxRoute = 9, numFxBuses = 4;
    // A send's TARGET, by index (saved: only append): 0 off, 1-8 BUS 1-8, 9-12 FX BUS A-D, 13 DELAY, 14 REVERB.
    inline const juce::StringArray sendTargetNames { "Off", "Bus 1", "Bus 2", "Bus 3", "Bus 4", "Bus 5", "Bus 6", "Bus 7", "Bus 8",
                                                     "FX Bus A", "FX Bus B", "FX Bus C", "FX Bus D", "Delay", "Reverb" };
    inline constexpr int delayTarget = 13, reverbTarget = 14;
    inline int busOfTarget (int target) noexcept { return target >= 1 && target <= numBuses ? target - 1 : -1; }   // a route's too
    inline int fxBusOfTarget (int target) noexcept { return target >= firstFxRoute && target < firstFxRoute + numFxBuses ? target - firstFxRoute : -1; }

    // Each bus's settings, as the kit plays them.
    struct BusSettings
    {
        float gain = 1.0f, pan = 0.0f;
        bool mute = false, solo = false, invert = false;
        int output = 0, route = 0;
    };

    // MUTE MODE (the mixer's menu): PRE SEND, a muted channel's sends are silent too; POST SEND, its pre-fader sends go
    // on (to hear a snare mic's reverb alone). Saved by index: only append.
    enum class MuteMode { preSend, postSend };
    inline const juce::StringArray muteModeNames { "Pre Send", "Post Send" };

    // A SEND: a copy of the channel into a target. A mic channel's DIRECT sound (the drums it's meant for) and its
    // BLEED (the rest) have their own on, level and pan, LINK moving the two levels together; a bus's sound is all
    // direct. POST FADER: after the channel's fader (and its mute); PRE FADER: before. Its own pan, not the channel's.
    enum class SendMode { postFader, preFader };
    inline const juce::StringArray sendModeNames { "Post Fader", "Pre Fader" };   // saved by index: only append
    struct Send
    {
        int target = 0;
        float directDb = 0.0f, bleedDb = 0.0f;   // -60 (off) to +12
        float directPan = 0.0f, bleedPan = 0.0f;
        bool directOn = true, bleedOn = true, link = true;
        SendMode mode = SendMode::postFader;

        bool isOn() const noexcept { return target > 0; }
        static float gainOf (bool on, float db) noexcept { return on && db > -59.9f ? juce::Decibels::decibelsToGain (db) : 0.0f; }
        bool operator== (const Send& o) const noexcept
        {
            const auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
            return target == o.target && near (directDb, o.directDb) && near (bleedDb, o.bleedDb) && near (directPan, o.directPan)
                && near (bleedPan, o.bleedPan) && directOn == o.directOn && bleedOn == o.bleedOn && link == o.link && mode == o.mode;
        }
    };
    inline constexpr int numSendSlots = 2;
    inline constexpr int numStrips = numChannels + numBuses;   // the channels, then the buses
    using SendTable = std::array<std::array<Send, numSendSlots>, numStrips>;

    // Whether strip `from` (a channel, or numChannels + a bus) may go into BUS `bus`: channels into any, a bus only
    // into a later one.
    inline bool canFeedBus (int from, int bus) noexcept
    {
        return bus >= 0 && bus < numBuses && (from < numChannels || bus > from - numChannels);
    }

    // BLEED FROM INSTRUMENTS (the mixer's properties): each channel's level for each piece of the kit (the drums
    // grouped as the articulation list groups them), its polarity, and whether it's on at all. A piece's own close
    // channel calls it its CLOSE MIC AUDIO.
    inline constexpr int numPieces = 14;   // drums::numPieces (kept in step by a test)
    struct BleedCell
    {
        float gainDb = 0.0f;           // -60 (off) to +12
        bool invert = false;
        bool on = true;
        bool operator== (const BleedCell& o) const noexcept { return std::abs (gainDb - o.gainDb) < 1.0e-4f && invert == o.invert && on == o.on; }
        float gain() const noexcept { return on && gainDb > -59.9f ? juce::Decibels::decibelsToGain (gainDb) * (invert ? -1.0f : 1.0f) : 0.0f; }
    };
    using BleedMatrix = std::array<std::array<BleedCell, numPieces>, numChannels>;
}
