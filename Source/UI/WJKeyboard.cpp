#include "WJKeyboard.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "../PluginProcessor.h"

namespace {
constexpr int kWhiteKeysPerOctave = 7;
constexpr int kWhiteSemitones[kWhiteKeysPerOctave] = { 0, 2, 4, 5, 7, 9, 11 };
// True for the white key immediately to the LEFT of a black key (C, D, F, G, A).
constexpr bool kHasBlackToRight[kWhiteKeysPerOctave] = { true, true, false, true, true, true, false };
constexpr int kMidiChannel = 1;

const juce::Colour kPanelBg      { 0xFF1A1A1A };
const juce::Colour kKeyWhite     { 0xFFF2EFE6 };
const juce::Colour kKeyWhiteHeld { 0xFF5C9CFF };
const juce::Colour kKeyBlack     { 0xFF151515 };
const juce::Colour kKeyBlackHeld { 0xFF2F6FE0 };
const juce::Colour kKeyBorder    { 0xFF8A8678 };
const juce::Colour kButtonFill   { 0xFF3A3A3A };
const juce::Colour kButtonText   { 0xFFDDDDDD };
const juce::Colour kCaption      { 0xFF9A9A9A };
} // namespace

// The classic two-row tracker layout (FastTracker2, Impulse Tracker, OpenMPT, Renoise).
// AZERTY's characters are what a French keyboard's PHYSICAL key at that same position
// actually sends: unshifted digits on AZERTY are punctuation, so the accidentals' upper
// row differs there, and three letters move (A/Q, W/Z, M/;).
const std::vector<WJKeyboard::TrackerKey>& WJKeyboard::trackerKeys()
{
    static const std::vector<TrackerKey> keys = {
        // lower row: Z S X D C V G B H N J M , L . ; /  (base octave, semitones 0..16)
        { 0, 'z', 'w' }, { 1, 's', 's' }, { 2, 'x', 'x' }, { 3, 'd', 'd' },
        { 4, 'c', 'c' }, { 5, 'v', 'v' }, { 6, 'g', 'g' }, { 7, 'b', 'b' },
        { 8, 'h', 'h' }, { 9, 'n', 'n' }, { 10, 'j', 'j' }, { 11, 'm', ',' },
        { 12, ',', ';' }, { 13, 'l', 'l' }, { 14, '.', ':' }, { 15, ';', 'm' }, { 16, '/', '!' },
        // upper row: Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P  (one octave up, semitones 12..28)
        { 12, 'q', 'a' }, { 13, '2', (juce::juce_wchar)0x00E9 /* é */ }, { 14, 'w', 'z' },
        { 15, '3', '"' }, { 16, 'e', 'e' }, { 17, 'r', 'r' }, { 18, '5', '(' },
        { 19, 't', 't' }, { 20, '6', '-' }, { 21, 'y', 'y' },
        { 22, '7', (juce::juce_wchar)0x00E8 /* è */ }, { 23, 'u', 'u' }, { 24, 'i', 'i' },
        { 25, '9', (juce::juce_wchar)0x00E7 /* ç */ }, { 26, 'o', 'o' },
        { 27, '0', (juce::juce_wchar)0x00E0 /* à */ }, { 28, 'p', 'p' },
    };
    return keys;
}

WJKeyboard::WJKeyboard(DW8000Processor& p) : proc(p)
{
    const auto& cfg = proc.keyboardSettings;
    pcKeyboardEnabled = cfg.pcInput;
    pcLayout   = cfg.pcLayout == 1 ? PcLayout::azerty : PcLayout::qwerty;
    numOctaves = juce::jlimit(0, 5, cfg.numOctaves);

    pcKeyDown.assign(trackerKeys().size(), false);
    pcReleaseSeenAt.assign(trackerKeys().size(), 0);
    setWantsKeyboardFocus(false);
    rebuildKeys();

    // Remote activity (MIDI In, arpeggiator) is asynchronous, so poll to light its keys.
    startTimerHz(30);
}

WJKeyboard::~WJKeyboard()
{
    stopTimer();
    releaseAllTouchNotes();
    releaseAllPcNotes();
    releaseAllHeldNotes();
}

void WJKeyboard::setNumOctaves(int n)
{
    n = juce::jlimit(0, 5, n);
    if (n == numOctaves) return;
    numOctaves = n;
    rebuildKeys();
    repaint();
}

void WJKeyboard::sendNote(int note, float velocity, bool on)
{
    if (note < 0 || note > 127) return;
    auto& ks = proc.getKeyboardState();
    if (on) ks.noteOn(kMidiChannel, note, velocity);
    else    ks.noteOff(kMidiChannel, note, 0.0f);
}

void WJKeyboard::releaseAllPcNotes()
{
    for (size_t i = 0; i < pcKeyDown.size(); ++i)
    {
        pcReleaseSeenAt[i] = 0;
        if (!pcKeyDown[i]) continue;
        pcKeyDown[i] = false;
        sendNote(kPcBaseNote + octaveShift * 12 + trackerKeys()[i].semitoneFromBase, 0.0f, false);
    }
}

void WJKeyboard::showContextMenu(int noteForHold)
{
    juce::PopupMenu layoutMenu;
    layoutMenu.addItem(2001, "QWERTY", true, pcLayout == PcLayout::qwerty);
    layoutMenu.addItem(2002, "AZERTY", true, pcLayout == PcLayout::azerty);

    juce::PopupMenu m;
    if (noteForHold >= 0)
    {
        m.addItem(5000, "Hold note (" + juce::MidiMessage::getMidiNoteName(noteForHold, true, true, 4) + ")",
                  true, heldNotes.count(noteForHold) > 0);
        m.addSeparator();
    }
    m.addItem(4000, "PC keyboard input (tracker-style)", true, pcKeyboardEnabled);
    m.addSubMenu("PC keyboard layout", layoutMenu, pcKeyboardEnabled);
    m.addSeparator();
    {
        const int velocity = proc.keyboardSettings.velocity;
        juce::PopupMenu velocityMenu;
        for (int v : { 60, 80, 100, 127 })
            velocityMenu.addItem(7000 + v, juce::String(v), true, velocity == v);
        m.addSubMenu("Velocity (" + juce::String(velocity) + ")", velocityMenu);
        m.addSeparator();
    }
    {
        juce::PopupMenu sizeMenu;
        sizeMenu.addItem(6005, "61 keys (5 octaves)", true, numOctaves == 5);
        sizeMenu.addItem(6002, "2 octaves", true, numOctaves == 2);
        sizeMenu.addItem(6000, "Resizable (more keys when wider)", true, numOctaves == 0);
        m.addSubMenu("Keyboard size", sizeMenu);
    }

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                    [this, noteForHold](int result)
    {
        if (result == 5000) { toggleHoldNote(noteForHold); return; }
        if (result > 7000 && result <= 7127) { proc.keyboardSettings.velocity = result - 7000; return; }
        if (result == 4000)
        {
            pcKeyboardEnabled = !pcKeyboardEnabled;
            proc.keyboardSettings.pcInput = pcKeyboardEnabled;
            if (pcKeyboardEnabled)
            {
                if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
                    editor->grabKeyboardFocus();
            }
            else releaseAllPcNotes();
            return;
        }
        if (result == 2001 || result == 2002)
        {
            pcLayout = result == 2001 ? PcLayout::qwerty : PcLayout::azerty;
            proc.keyboardSettings.pcLayout = result == 2001 ? 0 : 1;
            return;
        }
        if (result == 6000 || result == 6002 || result == 6005)
        {
            setNumOctaves(result - 6000);
            proc.keyboardSettings.numOctaves = numOctaves;
        }
    });
}

// A held note keeps sounding no matter what else happens on this component until toggled
// off the same way again.
void WJKeyboard::toggleHoldNote(int note)
{
    if (note < 0) return;
    if (heldNotes.count(note) > 0)
    {
        heldNotes.erase(note);
        sendNote(note, 0.0f, false);
    }
    else
    {
        heldNotes.insert(note);
        sendNote(note, proc.keyboardSettings.velocity / 127.0f, true);
    }
    repaint();
}

void WJKeyboard::releaseAllHeldNotes()
{
    for (int note : heldNotes) sendNote(note, 0.0f, false);
    heldNotes.clear();
}

bool WJKeyboard::keyStateChanged(bool /*isKeyDown*/)
{
    if (!pcKeyboardEnabled) return false;
    const auto& keys = trackerKeys();
    bool used = false;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const juce::juce_wchar c = pcLayout == PcLayout::qwerty ? keys[i].qwerty : keys[i].azerty;
        const bool down = juce::KeyPress::isKeyCurrentlyDown((int)c);
        if (down)
        {
            if (pcReleaseSeenAt[i] != 0) { pcReleaseSeenAt[i] = 0; used = true; continue; }  // auto-repeat
            if (pcKeyDown[i]) continue;
            pcKeyDown[i] = true;
            sendNote(kPcBaseNote + octaveShift * 12 + keys[i].semitoneFromBase,
                     proc.keyboardSettings.velocity / 127.0f, true);
            used = true;
        }
        else if (pcKeyDown[i] && pcReleaseSeenAt[i] == 0)
        {
            // Not released yet: timerCallback() does it if the key is still up shortly after.
            pcReleaseSeenAt[i] = juce::jmax<juce::uint32>(1, juce::Time::getMillisecondCounter());
            used = true;
        }
    }
    if (used) repaint();
    return used;
}

// Two physical keys can map to the same note (the rows overlap by an octave).
bool WJKeyboard::isPcKeyDownForNote(int note) const
{
    const auto& keys = trackerKeys();
    for (size_t i = 0; i < keys.size(); ++i)
        if (pcKeyDown[i] && kPcBaseNote + octaveShift * 12 + keys[i].semitoneFromBase == note) return true;
    return false;
}

void WJKeyboard::timerCallback()
{
    // Confirm the PC key releases seen by keyStateChanged() (see pcReleaseSeenAt).
    const auto& keys = trackerKeys();
    const auto now = juce::Time::getMillisecondCounter();
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (pcReleaseSeenAt[i] == 0) continue;
        const juce::juce_wchar c = pcLayout == PcLayout::qwerty ? keys[i].qwerty : keys[i].azerty;
        if (juce::KeyPress::isKeyCurrentlyDown((int)c)) { pcReleaseSeenAt[i] = 0; continue; }
        if (now - pcReleaseSeenAt[i] < 20) continue;
        pcReleaseSeenAt[i] = 0;
        pcKeyDown[i] = false;
        sendNote(kPcBaseNote + octaveShift * 12 + keys[i].semitoneFromBase, 0.0f, false);
    }

    // A held note can go silent without toggleHoldNote() (e.g. an All Notes Off).
    for (auto it = heldNotes.begin(); it != heldNotes.end();)
    {
        if (!proc.isNoteActive(*it)) it = heldNotes.erase(it);
        else ++it;
    }
    repaint();
}

void WJKeyboard::focusLost(juce::Component::FocusChangeType) { releaseAllPcNotes(); }

void WJKeyboard::changeOctave(int delta)
{
    const int shifted = juce::jlimit(-3, 2, octaveShift + delta);
    if (shifted == octaveShift) return;
    releaseAllTouchNotes();
    releaseAllPcNotes();  // held notes are note numbers already sent
    octaveShift = shifted;
    rebuildKeys();
    repaint();
}

// Geometry only. Called on resize and on octave change.
void WJKeyboard::rebuildKeys()
{
    whiteKeys.clear();
    blackKeys.clear();

    auto area = getLocalBounds().toFloat();
    captionBounds = area.removeFromTop(juce::jmin(16.0f, area.getHeight() * 0.16f));
    const float buttonW = juce::jmin(36.0f, area.getWidth() * 0.06f);
    octaveDownBounds = area.removeFromLeft(buttonW);
    octaveUpBounds   = area.removeFromRight(buttonW);
    keysBounds = area;

    if (keysBounds.getWidth() < 1.0f || keysBounds.getHeight() < 1.0f) return;

    int kNumWhite = numOctaves * kWhiteKeysPerOctave + 1;  // trailing C
    if (numOctaves == 0)
    {
        const int octaves = juce::jlimit(1, juce::jmax(1, (127 - (kLowestNote + octaveShift * 12)) / 12),
                                         (int)(keysBounds.getWidth() / (kResizableKeyW * kWhiteKeysPerOctave)));
        kNumWhite = octaves * kWhiteKeysPerOctave + 1;
    }
    const float whiteW = keysBounds.getWidth() / float(kNumWhite);
    const float blackW = whiteW * 0.62f;
    const float blackH = keysBounds.getHeight() * 0.6f;

    for (int i = 0; i < kNumWhite; ++i)
    {
        const int octaveIndex = i / kWhiteKeysPerOctave;
        const int local = i % kWhiteKeysPerOctave;
        const int note = kLowestNote + (octaveShift + octaveIndex) * 12 + kWhiteSemitones[local];
        const float x = keysBounds.getX() + i * whiteW;
        whiteKeys.push_back({ { x, keysBounds.getY(), whiteW, keysBounds.getHeight() }, note, false });

        if (i < kNumWhite - 1 && kHasBlackToRight[local])
        {
            const float bx = x + whiteW - blackW * 0.5f;
            blackKeys.push_back({ { bx, keysBounds.getY(), blackW, blackH }, note + 1, true });
        }
    }
}

int WJKeyboard::keyAt(juce::Point<float> p) const
{
    for (const auto& k : blackKeys)
        if (k.bounds.contains(p)) return k.note;
    for (const auto& k : whiteKeys)
        if (k.bounds.contains(p)) return k.note;
    return -1;
}

void WJKeyboard::setHeldNoteForSource(int sourceIndex, int note)
{
    const auto it = heldNoteBySource.find(sourceIndex);
    const int previous = (it != heldNoteBySource.end()) ? it->second : -1;
    if (note == previous) return;
    // Don't cut a note "Hold note" is deliberately sustaining.
    if (previous >= 0 && heldNotes.count(previous) == 0) sendNote(previous, 0.0f, false);
    if (note >= 0) heldNoteBySource[sourceIndex] = note;
    else if (it != heldNoteBySource.end()) heldNoteBySource.erase(it);
    if (note >= 0) sendNote(note, proc.keyboardSettings.velocity / 127.0f, true);
    repaint();
}

void WJKeyboard::releaseAllTouchNotes()
{
    for (const auto& kv : heldNoteBySource) sendNote(kv.second, 0.0f, false);
    heldNoteBySource.clear();
    repaint();
}

void WJKeyboard::paint(juce::Graphics& g)
{
    g.fillAll(kPanelBg);

    auto paintButton = [&](juce::Rectangle<float> b, const char* label)
    {
        g.setColour(kButtonFill);
        g.fillRect(b.reduced(3.0f));
        g.setColour(kButtonText);
        g.setFont(juce::FontOptions(juce::jlimit(12.0f, 22.0f, b.getWidth() * 0.5f)));
        g.drawText(label, b, juce::Justification::centred);
    };
    paintButton(octaveDownBounds, "-");
    paintButton(octaveUpBounds, "+");

    auto isTouchHeld = [this](int note)
    {
        for (const auto& kv : heldNoteBySource)
            if (kv.second == note) return true;
        return false;
    };
    auto isLit = [this, isTouchHeld](int note)
    {
        return isTouchHeld(note) || heldNotes.count(note) > 0 || isPcKeyDownForNote(note)
            || proc.isNoteActive(note);
    };
    for (const auto& k : whiteKeys)
    {
        g.setColour(isLit(k.note) ? kKeyWhiteHeld : kKeyWhite);
        g.fillRect(k.bounds.reduced(1.0f, 0.0f));
        g.setColour(kKeyBorder);
        g.drawRect(k.bounds, 1.0f);
    }
    for (const auto& k : blackKeys)
    {
        g.setColour(isLit(k.note) ? kKeyBlackHeld : kKeyBlack);
        g.fillRect(k.bounds);
    }

    // "C2"/"C3"/... above every C key.
    g.setFont(juce::FontOptions(juce::jlimit(8.0f, 11.0f, keysBounds.getHeight() * 0.12f)));
    for (const auto& k : whiteKeys)
    {
        if (k.note % 12 != 0) continue;
        g.setColour((isLit(k.note) ? kKeyWhiteHeld : kKeyWhite).contrasting(0.7f));
        g.drawText("C" + juce::String(k.note / 12 - 1), k.bounds.withTrimmedLeft(3.0f).removeFromTop(13.0f),
                   juce::Justification::centredLeft);
    }

    g.setColour(kCaption);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("OCT " + (octaveShift > 0 ? juce::String("+") + juce::String(octaveShift)
                                         : juce::String(octaveShift)),
               captionBounds, juce::Justification::centred);
}

void WJKeyboard::resized() { rebuildKeys(); }

void WJKeyboard::mouseDown(const juce::MouseEvent& e)
{
    // The key events go to the editor (see DW8000Editor::keyStateChanged); make sure it
    // has the focus, so that turning PC input on from the menu leaves typing ready.
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        editor->grabKeyboardFocus();

    const int note = keyAt(e.position);
    if (e.mods.isPopupMenu()) { showContextMenu(note); return; }
    if (octaveDownBounds.contains(e.position)) { changeOctave(-1); return; }
    if (octaveUpBounds.contains(e.position))   { changeOctave(1);  return; }
    if (note < 0) return;
    setHeldNoteForSource(e.source.getIndex(), note);
}

void WJKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    const int sourceIndex = e.source.getIndex();
    if (heldNoteBySource.find(sourceIndex) == heldNoteBySource.end())
        return;  // this source's mouseDown didn't start on a key
    setHeldNoteForSource(sourceIndex, keyAt(e.position));
}

void WJKeyboard::mouseUp(const juce::MouseEvent& e)   { setHeldNoteForSource(e.source.getIndex(), -1); }
void WJKeyboard::mouseExit(const juce::MouseEvent& e) { setHeldNoteForSource(e.source.getIndex(), -1); }
