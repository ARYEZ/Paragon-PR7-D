#include "EqMatchSession.h"

namespace eq::match
{
    namespace
    {
        constexpr auto extension = ".pr7ref";
    }

    Session::Session (juce::File referencesFolder) : folder (std::move (referencesFolder)) {}

    Session::~Session()
    {
        stopLoading = true;   // the future's destructor waits for the file to give up
    }

    void Session::setRecording (Recording newRecording) noexcept
    {
        if (newRecording == Recording::reference && recording != Recording::reference)
        {
            // Listening for a reference replaces a file or saved one.
            referenceFixed.reset();
            referenceName = "Input";
        }
        recording = newRecording;
        lastTotal = -1;   // only audio from now on
        ++version;
    }

    void Session::update (const InputTap& tap)
    {
        // A file finished loading?
        if (loading.valid() && loading.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
        {
            if (auto spectrum = loading.get(); spectrum.has_value() && spectrum->isValid())
            {
                referenceFixed = std::move (spectrum);
                referenceInput.reset();
                referenceName = loadingFile.getFileNameWithoutExtension();
                problem.clear();
            }
            else
            {
                problem = loadingFile.getFileName() + (spectrum.has_value() ? " is too short or too quiet to match"
                                                                            : " couldn't be read as audio");
            }
            ++version;
        }

        const auto total = tap.getTotalWritten();
        if (recording == Recording::none || tap.getSampleRate() <= 0.0)
        {
            lastTotal = total;
            return;
        }
        if (lastTotal < 0)
            lastTotal = total;
        const auto fresh = (int) std::min<std::int64_t> (total - lastTotal, tap.capacity());
        lastTotal = total;
        if (fresh <= 0)
            return;
        scratch.resize ((size_t) fresh);
        tap.readLatest (scratch.data(), fresh);
        (recording == Recording::track ? track : referenceInput).add (scratch.data(), fresh, tap.getSampleRate());
        ++version;
    }

    void Session::clearTrack()
    {
        track.reset();
        ++version;
    }

    void Session::clearReference()
    {
        referenceInput.reset();
        referenceFixed.reset();
        referenceName.clear();
        if (recording == Recording::reference)
            referenceName = "Input";
        ++version;
    }

    Spectrum Session::getReference() const
    {
        return referenceFixed.has_value() ? *referenceFixed : referenceInput.spectrum();
    }

    void Session::loadFile (const juce::File& file)
    {
        if (loading.valid())
            return;   // one at a time
        if (recording == Recording::reference)
            recording = Recording::none;
        loadingFile = file;
        stopLoading = false;
        problem.clear();
        loading = std::async (std::launch::async, [file, this] { return analyseFile (file, [this] { return stopLoading.load(); }); });
        ++version;
    }

    juce::Array<juce::File> Session::getSavedReferences() const
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
        files.sort();
        return files;
    }

    bool Session::save (const Spectrum& spectrum, const juce::String& name)
    {
        if (! spectrum.isValid() || name.trim().isEmpty())
            return false;
        if (! folder.createDirectory())
            return false;
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name.trim()) + extension);
        const auto saved = spectrum.toXml (name.trim())->writeTo (file);
        ++version;
        return saved;
    }

    bool Session::useSaved (const juce::File& file)
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return false;
        auto spectrum = Spectrum::fromXml (*xml);
        if (! spectrum.has_value() || ! spectrum->isValid())
            return false;
        if (recording == Recording::reference)
            recording = Recording::none;
        referenceFixed = std::move (spectrum);
        referenceInput.reset();
        referenceName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        ++version;
        return true;
    }
}
