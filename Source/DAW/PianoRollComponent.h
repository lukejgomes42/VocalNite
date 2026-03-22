#pragma once
#include <JuceHeader.h>

class PianoRollComponent : public juce::Component,
    public juce::ScrollBar::Listener
{
public:
    PianoRollComponent(int patternId = -1);
    ~PianoRollComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ScrollBar listener
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    int currentPatternId = -1;

    // Layout constants
    static constexpr int keyWidth = 80;
    static constexpr int noteHeight = 16;
    static constexpr int numOctaves = 8;
    static constexpr int numKeys = numOctaves * 12;
    static constexpr int cellWidth = 40;
    static constexpr int numBeats = 32;
    static constexpr int headerHeight = 24;

    // Scrollbars
    juce::ScrollBar verticalScroll{ true };
    juce::ScrollBar horizontalScroll{ false };

    // Scroll offsets
    double verticalOffset = 0.0;
    double horizontalOffset = 0.0;

    // Placed notes storage
    struct Note
    {
        int pitch;
        int beat;
        juce::String lyric;
    };
    juce::Array<Note> placedNotes;

    juce::TextEditor lyricEditor;
    int editingNoteIndex = -1;

    // Helper functions
    bool isBlackKey(int noteIndex) const;
    juce::Colour getNoteColour(int pitch) const;
    void saveNote(int pitch, int beat, const juce::String& lyric = "");
    void deleteNote(int pitch, int beat);
    void loadNotes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};