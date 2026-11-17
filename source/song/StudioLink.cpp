#include "StudioLink.h"

namespace song::link
{
    namespace
    {
        constexpr std::array<juce::uint8, 5> header { 0x7D, 'P', 'R', '7', 'S' };
    }

    std::optional<Message> parse (const juce::uint8* data, int size)
    {
        if (data == nullptr || size < (int) header.size() + 1)
            return std::nullopt;
        for (size_t i = 0; i < header.size(); ++i)
            if (data[i] != header[i])
                return std::nullopt;
        const auto command = (int) data[header.size()];
        if (command < 1 || command > 3)
            return std::nullopt;
        Message m;
        m.command = (Command) command;
        if (m.command == Command::load)
        {
            const auto* payload = data + header.size() + 1;
            const auto length = size - (int) header.size() - 1;
            if (length <= 0 || length % 2 != 0)
                return std::nullopt;
            juce::MemoryBlock utf8;
            for (int i = 0; i < length; i += 2)
            {
                if (payload[i] > 0x0F || payload[i + 1] > 0x0F)
                    return std::nullopt;
                const auto byte = (juce::uint8) ((payload[i] << 4) | payload[i + 1]);
                utf8.append (&byte, 1);
            }
            m.path = juce::String::fromUTF8 ((const char*) utf8.getData(), (int) utf8.getSize());
            if (m.path.isEmpty())
                return std::nullopt;
        }
        return m;
    }

    juce::MidiMessage make (Command command, const juce::String& path)
    {
        juce::MemoryBlock data (header.data(), header.size());
        const auto c = (juce::uint8) command;
        data.append (&c, 1);
        if (command == Command::load)
        {
            const auto utf8 = path.toRawUTF8();
            for (size_t i = 0; utf8[i] != 0; ++i)
            {
                const auto byte = (juce::uint8) utf8[i];
                const juce::uint8 halves[2] { (juce::uint8) (byte >> 4), (juce::uint8) (byte & 0x0F) };
                data.append (halves, 2);
            }
        }
        return juce::MidiMessage::createSysExMessage (data.getData(), (int) data.getSize());
    }

    bool Mailbox::offer (const juce::uint8* data, int length) noexcept
    {
        if (data == nullptr || length <= 0 || length > (int) bytes->size())
            return false;
        auto expected = 0;
        if (! state.compare_exchange_strong (expected, 1, std::memory_order_acquire))
            return false;
        std::copy (data, data + length, bytes->begin());
        size = length;
        state.store (2, std::memory_order_release);
        return true;
    }

    std::optional<Message> Mailbox::take()
    {
        if (state.load (std::memory_order_acquire) != 2)
            return std::nullopt;
        auto message = parse (bytes->data(), size);
        state.store (0, std::memory_order_release);
        return message;
    }
}
