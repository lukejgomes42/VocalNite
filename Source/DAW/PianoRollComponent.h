#pragma once
#include <JuceHeader.h>
#include "../Educational/EducationalModeManager.h"

class PianoRollComponent : public juce::Component,
    public juce::ScrollBar::Listener,
    public juce::SettableTooltipClient,
    public EducationalModeManager::Listener
{
public:
    PianoRollComponent(int patternId = -1);
    ~PianoRollComponent() override;

    std::function<void()> onEditorClosed;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void educationalModeChanged(bool isEnabled) override;

private:
    int currentPatternId = -1;

    // Layout constants
    static constexpr int keyWidth = 80;
    static constexpr int numKeys = 15;     // C, F#, A across octaves 2-6
    static constexpr int cellWidth = 40;
    static constexpr int numBeats = 32;
    static constexpr int headerHeight = 24;

    // noteHeight is dynamic — recomputed in resized() to fill the available
    // viewport so we don't waste space at the bottom on tall windows.
    int noteHeight = 28;

    // Voice-bank note classes (C, F#, A) repeated over 5 octaves.
    // Row 0 = highest pitch (A6), row 14 = lowest (C2).
    // Bank folder rows are: row 5 = C5, row 7 = F#4, row 9 = A3.
    static constexpr int kRowMidi[15] = {
        93, 90, 84, 81, 78, 72, 69, 66, 60, 57, 54, 48, 45, 42, 36
    };
    static constexpr const char* kRowNames[15] = {
        "A6","F#6","C6","A5","F#5","C5","A4","F#4","C4","A3","F#3","C3","A2","F#2","C2"
    };
    static bool rowIsSharp(int row) { return row % 3 == 1; }
    static bool rowIsBankNote(int row) { return row == 5 || row == 7 || row == 9; }

    // Scrollbars
    juce::ScrollBar verticalScroll{ true };
    juce::ScrollBar horizontalScroll{ false };

    double verticalOffset = 0.0;
    double horizontalOffset = 0.0;

    struct Note
    {
        int pitch;
        int beat;
        juce::String lyric;
        int duration = 1;     // legacy field — kept for DB compatibility
    };
    juce::Array<Note> placedNotes;

    // Note drag state
    bool isDraggingNote = false;
    int  draggingNoteIndex = -1;
    int  dragStartMouseX = 0;
    int  dragStartMouseY = 0;
    int  dragOriginalBeat = 0;
    int  dragOriginalPitch = 0;

    juce::TextEditor lyricEditor;
    int editingNoteIndex = -1;
    bool suppressNextFocusLost = false;

    bool isBlackKey(int noteIndex) const;
    juce::Colour getNoteColour(int pitch) const;
    void saveNote(int pitch, int beat, const juce::String& lyric = "", int duration = 1);
    void deleteNote(int pitch, int beat);
    void loadNotes();
    void applyTooltips(bool eduEnabled);

    void commitCurrentLyricEdit();
    void discardCurrentLyricEdit();
    void positionLyricEditorForEditingNote();
    int  findNoteIndexAtMouse(const juce::MouseEvent& e) const;

    int    getViewportWidth()  const;
    int    getViewportHeight() const;
    double getMaxVerticalOffset()   const;
    double getMaxHorizontalOffset() const;
    void   clampOffsetsToViewport();
    void   centreVerticallyOnMidi(int midi);

    bool initialScrollApplied = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};