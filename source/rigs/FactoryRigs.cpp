#include "FactoryRigs.h"

std::vector<Rig> makeFactoryRigs()
{
    // PR7-B's bass rigs didn't come along. PR7-E gets its own factory rigs
    // once its stomps are built; until then, one empty starting point.
    Rig init;
    init.name = "Init";
    init.bank = 0;   // A01
    init.number = 1;
    init.tags = { "Init" };
    init.notes = "Everything off: a starting point.";
    return { init };
}
