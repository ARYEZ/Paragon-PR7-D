#pragma once

// 0.92.0: SYNCED TO DAW. A VST3 plugin can't start or stop its DAW (JUCE's transport control is iOS-only), but a key
// the plugin doesn't use goes on to the DAW: JUCE posts it to the window the plugin's own window sits in. The GRID
// EDITOR's and GROOVES' own windows aren't inside the DAW's, so their SPACE is posted the same way, from the plugin's
// window. Windows only; elsewhere this does nothing.
namespace dawkeys
{
    void postSpaceToParent (void* nativeHandle);
}
