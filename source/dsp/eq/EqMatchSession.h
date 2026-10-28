#pragma once

#include "EqMatch.h"
#include "dsp/InputTap.h"

#include <future>

// EQ Match's listening, for the EQ page (message thread). It belongs to the
// processor, so what's been heard survives closing the window, and it
// listens from the processor's timer (to the EQ's input), as the tuner does.
//
//   - YOUR TRACK: the EQ's input while it's recording.
//   - REFERENCE: the EQ's input too (another part of the song: last week's
//     vocal take), an audio file (analysed in the background), or a saved
//     reference (a small file in the rigs folder's "Match References").
namespace eq::match
{
    class Session
    {
    public:
        explicit Session (juce::File referencesFolder);
        ~Session();

        enum class Recording { none, track, reference };
        void setRecording (Recording) noexcept;
        Recording getRecording() const noexcept { return recording; }

        void update (const InputTap&);   // the processor's timer

        void clearTrack();
        void clearReference();
        Spectrum getTrack() const { return track.spectrum(); }
        Spectrum getReference() const;
        juce::String getReferenceName() const { return referenceName; }   // "" when there's none

        // An audio file as the reference, analysed in the background.
        void loadFile (const juce::File&);
        bool isLoading() const noexcept { return loading.valid(); }
        juce::String getProblem() const { return problem; }   // why the last file didn't load

        // Saved references.
        juce::File getReferencesFolder() const { return folder; }
        void setReferencesFolder (juce::File newFolder) { folder = std::move (newFolder); ++version; }
        juce::Array<juce::File> getSavedReferences() const;
        bool save (const Spectrum&, const juce::String& name);   // into the folder
        bool useSaved (const juce::File&);

        // Bumped whenever anything above changes, for the page to notice.
        int getVersion() const noexcept { return version; }

    private:
        juce::File folder;
        Recording recording = Recording::none;
        Analyser track, referenceInput;
        std::optional<Spectrum> referenceFixed;   // from a file or a saved reference
        juce::String referenceName, problem;
        std::int64_t lastTotal = -1;
        std::vector<float> scratch;
        int version = 0;

        std::future<std::optional<Spectrum>> loading;
        std::atomic<bool> stopLoading { false };
        juce::File loadingFile;
    };
}
