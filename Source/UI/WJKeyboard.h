#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <set>
#include <vector>

class DW8000Processor;

// On-screen keyboard: a mouse piano plus optional tracker-style PC keyboard input
// (QWERTY/AZERTY, FastTracker2/Impulse Tracker convention). Right-click for the menu.
// Adapted from the D110Keyboard of the Di-111 synth, with the same behaviour.
//
// The default span is the DW-8000's own 61 keys, C2 to C7 (MIDI 36 to 96). Notes struck
// here go through DW8000Processor::getKeyboardState(), so the engine, the arpeggiator and
// the lit keys see them like any MIDI note.
//
// PC keyboard layout: two overlapping rows, the lower one ZSXDCVGBHNJM,L.;/ starting at
// the current base octave, the upper one Q2W3ER5T6Y7UI9O0P one octave above it. QWERTY and
// AZERTY differ only in the character a physical key sends, not in the note it plays.
class WJKeyboard : public juce::Component, private juce::Timer
{
public:
    explicit WJKeyboard(DW8000Processor&);
    ~WJKeyboard() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(juce::Component::FocusChangeType) override;

    // 0 = resizable (fixed key width, as many keys as the width allows), 1..5 octaves.
    void setNumOctaves(int n);
    int  getNumOctaves() const { return numOctaves; }

    void showContextMenu(int noteForHold = -1);

private:
    static constexpr int   kLowestNote    = 36;  // C2
    static constexpr int   kPcBaseNote    = 48;  // C3: first note of the PC keyboard's lower row
    static constexpr float kResizableKeyW = 24.0f;

    enum class PcLayout { qwerty, azerty };

    struct KeyRect { juce::Rectangle<float> bounds; int note; bool black; };
    struct TrackerKey { int semitoneFromBase; juce::juce_wchar qwerty; juce::juce_wchar azerty; };
    static const std::vector<TrackerKey>& trackerKeys();

    void rebuildKeys();
    int  keyAt(juce::Point<float>) const;
    void setHeldNoteForSource(int sourceIndex, int note);
    void releaseAllTouchNotes();
    void changeOctave(int delta);
    void sendNote(int note, float velocity, bool on);
    void releaseAllPcNotes();
    bool isPcKeyDownForNote(int note) const;
    void toggleHoldNote(int note);
    void releaseAllHeldNotes();
    void timerCallback() override;

    DW8000Processor& proc;
    int numOctaves  = 5;
    int octaveShift = 0;
    std::map<int, int> heldNoteBySource;  // mouse/touch source index -> held note
    std::set<int> heldNotes;              // "Hold note" from the menu

    bool pcKeyboardEnabled = false;
    PcLayout pcLayout = PcLayout::qwerty;
    std::vector<bool> pcKeyDown;
    // Time (ms) at which a PC key was first seen released, 0 if it is still down. The release
    // is only acted upon if the key is still up a few ms later: holding a key makes the OS
    // auto-repeat it, which looks like a release immediately followed by a new press.
    std::vector<juce::uint32> pcReleaseSeenAt;

    juce::Rectangle<float> captionBounds, octaveDownBounds, octaveUpBounds, keysBounds;
    std::vector<KeyRect> whiteKeys, blackKeys;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WJKeyboard)
};
