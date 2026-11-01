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
    };

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
