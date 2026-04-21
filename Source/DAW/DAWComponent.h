#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "../Audio/VocalSynthEngine.h"
#include "../Educational/EducationalModeManager.h"
#include "../Educational/SynthesisInspector.h"
#include "../Educational/HighlightOverlay.h"
#include "../Educational/TooltipRegistry.h"

// ──────────────────────────────────────────────────────────────────────────
//  SynthesisInspectorWindow: floating, non-modal window that hosts the
//  inspector panel. Owned by DAWComponent; the inspector content is owned
//  by the DAWComponent (as a member) and shared here via setContentNonOwned.
// ──────────────────────────────────────────────────────────────────────────
class SynthesisInspectorWindow : public juce::DocumentWindow
{
public:
    SynthesisInspectorWindow(SynthesisInspector* inspector)
        : DocumentWindow("Synthesis Inspector",
            juce::Colour(15, 15, 25),
            DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, false);
        setContentNonOwned(inspector, true);
        centreWithSize(560, 360);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        if (onClose) onClose();
    }

    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisInspectorWindow)
};

class DAWComponent : public juce::Component,
    public juce::MenuBarModel,
    public juce::ScrollBar::Listener,
    public juce::Timer,
    public EducationalModeManager::Listener
{
public:
    DAWComponent(const juce::String& projectName, int projectId, const juce::String& username = "");
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

    // Playhead
    void timerCallback() override;

    // Pattern Drag and Drop
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    std::function<void()> onReturnToDashboard;

    void setUsername(const juce::String& name) { currentUsername = name; usernameLabel.setText(name, juce::dontSendNotification); }

    // EducationalModeManager::Listener
    void educationalModeChanged(bool isEnabled) override;

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
    juce::Array<int> trackIds;
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

    // Pattern note previews (pitch + beat + duration, for clip rendering)
    struct NotePreview
    {
        int pitch;
        int beat;
        int duration = 1;
    };
    juce::Array<juce::Array<NotePreview>> patternNotePreviews;

    void loadPatternNotes();

    // Placed clips
    struct PlacedClip
    {
        int patternIndex;
        int trackIndex;
        double startBeat;
        double duration = 4.0;
        int clipId = -1;
    };
    juce::Array<PlacedClip> placedClips;

    void saveClip(const PlacedClip& clip);
    void deleteClip(int clipId);
    void loadClips();
    void updateClip(const PlacedClip& clip);

    void saveTrack(const juce::String& trackName, int orderIndex);
    void deleteTrackFromDB(int trackId);
    void loadTracks();

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

    // Metronome (audio-thread metronome lives inside vocalSynth)
    juce::TextButton metronomeButton;
    bool metronomeEnabled = false;   // local UI state mirror
    bool metronomeBeat = false;      // visual flash flag
    juce::AudioDeviceManager audioDeviceManager;

    void parseTimeSignature(const juce::String& timeSig, int& num, int& den) const;

    juce::String currentUsername;
    juce::Label usernameLabel;

    // ── Vocal Synthesis ──
    VocalSynthEngine vocalSynth;
    juce::AudioSourcePlayer synthPlayer;

    struct FullNote
    {
        int pitch;
        int beat;
        juce::String lyric;
    };
    juce::Array<juce::Array<FullNote>> patternFullNotes;

    int lastTriggeredBeat = -1;

    void loadFullPatternNotes();
    void triggerNotesAtBeat(int beat);

    // Auto-sized pattern durations (recomputed from note content)
    juce::Array<double> patternDurations;
    double getPatternDuration(int patternIndex) const;
    void updateClipDurations();  // sync placedClips[].duration with patternDurations[], persist changes

    // Helpers
    void addTrack();
    void removeTrack(int index);
    void addPattern();
    void openPatternEditor(int index);
    void drawPatternBrowser(juce::Graphics& g, int gridTop, int gridLeft);
    void loadPatterns();

    // ── Educational Mode ────────────────────────────────────────────────────
    SynthesisInspector        synthInspector;
    HighlightOverlay          highlightOverlay;
    juce::TooltipWindow       tooltipWindow{ this, 600 };  // 600ms hover delay
    juce::TextButton          inspectorToggleButton;
    SynthesisInspectorWindow* inspectorWindow = nullptr;
    int                       inspectedPatternIndex = -1;  // -1 when not inspecting

    bool isInspecting() const { return inspectorWindow != nullptr; }

    void updateTooltips(bool eduEnabled);
    void openInspectorForPattern(int patternIndex);
    void closeInspectorWindow();
    void showInspectorPatternPicker();

    // Build the unique-words-by-first-beat list for a pattern
    void buildInspectorWordList(int patternIndex,
        juce::StringArray& outWords,
        juce::StringArray& outPitches) const;

    // Convert grid pitch (0=top, 95=bottom) to a note name like "C4"
    static juce::String gridPitchToNoteName(int gridPitch);

    // ── Async resource loading ──────────────────────────────────────────────
    // The voice bank (~3400 WAV files) takes several seconds to load off disk.
    // We do it on a background thread and keep the UI responsive.

    class ResourceLoader : public juce::Thread
    {
    public:
        ResourceLoader(DAWComponent& owner, const juce::File& resources)
            : juce::Thread("VocalNite Resource Loader"),
            owner(owner), resourcesDir(resources) {
        }

        void run() override;
    private:
        DAWComponent& owner;
        juce::File    resourcesDir;
    };

    std::unique_ptr<ResourceLoader> resourceLoader;
    std::atomic<bool> isVocalBankReady{ false };
    std::atomic<bool> isDying{ false };   // set in dtor; callbacks check this before touching members

    // Called on the message thread when each loading stage completes
    void onDictionaryLoaded();
    void onVoiceBankLoaded();

    // Loading overlay — semi-transparent panel shown until the voice bank is ready
    class LoadingOverlay : public juce::Component, private juce::Timer
    {
    public:
        LoadingOverlay();
        void paint(juce::Graphics& g) override;
        void setStatus(const juce::String& s);
    private:
        void timerCallback() override;
        juce::String statusText{ "Loading your project..." };
        float  animPhase = 0.0f;
    };
    LoadingOverlay loadingOverlay;

    // Enable/disable transport + inspector buttons based on isVocalBankReady
    void refreshTransportEnabled();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWComponent)
};