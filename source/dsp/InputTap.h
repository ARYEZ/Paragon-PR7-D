#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Lets the tuner look at the incoming signal: the audio thread writes the
// input into a ring buffer, and the UI thread copies out the most recent
// samples whenever it wants to measure the pitch. Lock-free, and the audio
// thread never waits.
class InputTap
{
public:
    explicit InputTap (int capacity = 16384) : samples ((size_t) capacity) {}

    // Audio thread.
    void write (const float* data, int numSamples) noexcept
    {
        const auto capacity = (int) samples.size();
        auto position = writePosition.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            samples[(size_t) position].store (data[i], std::memory_order_relaxed);
            position = (position + 1) % capacity;
        }
        writePosition.store (position, std::memory_order_release);
        totalWritten.fetch_add (numSamples, std::memory_order_release);
    }

    // Audio thread: a stereo signal, as the middle of the two channels
    // ((left + right) / 2), so a part panned anywhere is heard.
    void write (const float* left, const float* right, int numSamples) noexcept
    {
        const auto capacity = (int) samples.size();
        auto position = writePosition.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            samples[(size_t) position].store (0.5f * (left[i] + right[i]), std::memory_order_relaxed);
            position = (position + 1) % capacity;
        }
        writePosition.store (position, std::memory_order_release);
        totalWritten.fetch_add (numSamples, std::memory_order_release);
    }

    // How many samples have been written since the start: it stops growing
    // when the DAW stops sending audio, so readers can tell new audio from old.
    std::int64_t getTotalWritten() const noexcept { return totalWritten.load (std::memory_order_acquire); }

    // Any other thread: copies the newest `count` samples, oldest first.
    void readLatest (float* destination, int count) const noexcept
    {
        const auto capacity = (int) samples.size();
        const auto end = writePosition.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            destination[i] = samples[(size_t) ((end - count + i + capacity * 2) % capacity)].load (std::memory_order_relaxed);
    }

    int capacity() const noexcept { return (int) samples.size(); }

    // The sample rate the audio is written at (0 before the plugin is prepared).
    void setSampleRate (double newRate) noexcept { rate = newRate; }
    double getSampleRate() const noexcept { return rate.load(); }

private:
    std::vector<std::atomic<float>> samples;
    std::atomic<int> writePosition { 0 };
    std::atomic<std::int64_t> totalWritten { 0 };
    std::atomic<double> rate { 0.0 };
};
