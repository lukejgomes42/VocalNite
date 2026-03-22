#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

class DAWComponent : public juce::Component,
    public juce::MenuBarModel,
    public juce::ScrollBar::Listener,
    public juce::Timer
{
public:
    DAWComponent(const juce::String& projectName, int projectId);
    ~DAWComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // ScrollBar
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    //Playhead
    void timerCallback() override;

    //Pattern Drag and Drop
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    std::function<void()> onReturnToDashboard;

private:
    // Menu bar
    juce::MenuBarComponent menuBar;

    // Toolbar area
    juce::Label projectNameLabel;
    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton stopButton;
    juce::TextButton selectModeButton;
    juce::TextButton editModeButton;
    juce::TextButton tempoButton;
    juce::TextButton timeSigButton;
    juce::Label logoLabel;

    // Dynamic tracks
    juce::TextButton addTrackButton;
    juce::StringArray trackNames;
    int trackHeight = 40;
    double trackScrollOffset = 0.0;
    juce::ScrollBar trackScrollBar{ true };

    // Pattern browser
    juce::StringArray patternNames;
    juce::TextButton addPatternButton;
    int patternHeight = 36;
    int editingPatternIndex = -1;
    juce::TextEditor patternRenameEditor;
    juce::ScrollBar patternScrollBar{ true };
    double patternScrollOffset = 0.0;
    juce::ScrollBar horizontalScrollBar{ false };
    double horizontalScrollOffset = 0.0;
    double playheadPosition = 0.0; // in beats
    bool isPlaying = false;
    int currentBPM = 120;
    juce::String currentTimeSig = "4/4";

    // Piano roll (opened from pattern editor)
    PianoRollComponent pianoRoll;
    bool pianoRollVisible = false;

    juce::String currentProjectName;
    int currentProjectId = -1;

    // Drag and drop
    int draggingPatternIndex = -1;
    bool isDraggingPattern = false;
    int dragX = 0;
    int dragY = 0;
    int draggingClipIndex = -1;
    bool isDraggingClip = false;
    float cellWidthMultiplier = 1.0f;

    juce::Array<int> patternIds;

    // Placed clips
    struct PlacedClip
    {
        int patternIndex;
        int trackIndex;
        double startBeat;
        double duration = 4.0; // default 4 beats
    };
    juce::Array<PlacedClip> placedClips;

    // Undo/Redo
    struct Action
    {
        enum Type { AddPattern, RemovePattern, AddClip, RemoveClip, MoveClip, AddTrack, RemoveTrack };
        Type type;

        // Pattern data
        juce::String patternName;
        int patternId = -1;
        int patternIndex = -1;

        // Clip data
        PlacedClip clip;
        PlacedClip previousClip;
        int clipIndex = -1;

        // Track data
        juce::String trackName;
        int trackIndex = -1;
    };

    juce::Array<Action> undoStack;
    juce::Array<Action> redoStack;

    void performUndo();
    void performRedo();

    // Helpers
    void addTrack();
    void removeTrack(int index);
    void addPattern();
    void openPatternEditor(int index);
    void drawPatternBrowser(juce::Graphics& g, int gridTop, int gridLeft);
    void loadPatterns();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWComponent)
};