// THE STUDIO LINK (PR7-D 0.83.0, the owner: "full integration" with Paragon PR7-S, the family's studio, which ships
// PR7-D as its house drum kit). PR7-S's composer lays a song of grooves out for a PR7-D track: it writes the song to a
// file (this plug-in's own song XML, Song::toXml's) and sends this instance, on its track, a SysEx message saying "load
// that song". PR7-D swaps the song in (the kit isn't touched), FOLLOW DAW on, so it plays with PR7-S's playhead; the file
// is remembered in the song (Song::studioLink), and edits made here are written back to it, so PR7-S sees them.
//
// The messages (our own; 0x7D is MIDI's "non-commercial" manufacturer number, never sold equipment's), between F0 and F7:
//   7D 50 52 37 53 cmd [payload]      ("PR7S", then the command)
//   cmd 01 load: payload = the file's full path, UTF-8, each byte as two 7-bit bytes (its high and low half);
//   cmd 02 pause: the song track stays silent (PR7-S is playing a drum idea of its own through this kit for a moment);
//   cmd 03 resume.
// A plug-in has its own copy of JUCE, so a MIDI message on its track is the one way in that every host passes on.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace song::link
{
    enum class Command { load = 1, pause = 2, resume = 3 };

    struct Message
    {
        Command command = Command::load;
        juce::String path;   // (load)
    };

    // A SysEx message's data (between F0 and F7) as one of ours; anything else: none.
    std::optional<Message> parse (const juce::uint8* data, int size);
    // One of ours as a MIDI message (PR7-S sends the same bytes; the tests make them here).
    juce::MidiMessage make (Command, const juce::String& path = {});

    // Load messages from the audio thread to the message thread: the audio thread copies one in (no allocation, no lock;
    // a second one while the first waits replaces it only once it's been taken), the timer takes it.
    class Mailbox
    {
    public:
        bool offer (const juce::uint8* data, int size) noexcept;   // audio thread: false if it's busy or too long
        std::optional<Message> take();                             // message thread

    private:
        // On the heap (made once, here): the processor stays small enough for Windows' 1 MB thread stack.
        std::unique_ptr<std::array<juce::uint8, 4096>> bytes = std::make_unique<std::array<juce::uint8, 4096>>();
        int size = 0;
        std::atomic<int> state { 0 };   // 0 empty, 1 being written, 2 full
    };
}
