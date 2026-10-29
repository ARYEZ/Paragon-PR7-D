// What gets copied onto the stack stays small. Windows gives a thread 1 MB of stack (Linux 8 MB), and a
// preset, its patterns and the kit's settings are copied there for undo, loading and each audio block (in PR7-K:
// 0.15.0's arp patterns, 40 KB inline in ModMatrix, made every test that built the plugin crash on Windows
// while Linux passed). (On Linux, `ulimit -s 1024` before the tests runs them with Windows' stack.)
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Stack: presets, the patterns, the kit's settings and the kit stay small enough to copy on Windows' stack", "[stack]")
{
    CHECK (sizeof (ModMatrix) < 16 * 1024);
    CHECK (sizeof (Rig) < 16 * 1024);
    CHECK (sizeof (synth::drums::KitSettings) < 16 * 1024);
    CHECK (sizeof (synth::drums::Kit) < 64 * 1024);
    CHECK (sizeof (synth::drumseq::Settings) < 16 * 1024);
    CHECK (sizeof (ParagonX1AudioProcessor) < 128 * 1024);
}
