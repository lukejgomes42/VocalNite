#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "../Audio/VocalSynthEngine.h"
#include "../Educational/EducationalModeManager.h"
#include "../Educational/SynthesisInspector.h"
#include "../Educational/HighlightOverlay.h"
#include "../Educational/TooltipRegistry.h"
#include "../UI/VoiceBankSelectorOverlay.h"

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
    public juce::TooltipClient,
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

    // juce::TooltipClient — supplies dynamic tooltips for hovered areas of
    // the DAW that aren't standalone components (e.g. patterns drawn into
    // the browser, clips drawn into the timeline). Static-control tooltips
    // (buttons, scrollbars) are still set via setTooltip on those components.
    juce::String getTooltip() override;

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

    // Same race-guard pattern as PianoRollComponent's lyric editor: when the
    // pattern rename editor is hidden mid-flow (e.g. swapping to a new rename
    // session, or programmatic dismissal), JUCE posts an async focus-loss
    // that would re-fire commit and clobber the next pattern's name. We
    // suppress one such event on managed transitions and clear via callAsync.
    bool suppressNextRenameFocusLost = false;
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

    void saveClip(PlacedClip& clip);       // populates clip.clipId on success
    void deleteClip(int clipId);
    void loadClips();
    void updateClip(const PlacedClip& clip);

    void saveTrack(const juce::String& trackName, int orderIndex);
    void deleteTrackFromDB(int trackId);
    void loadTracks();

    // Persistence for project-wide settings stored on Projects row
    void loadProjectSettings();           // called once from ctor; reads bpm + time_signature
    void saveProjectSettings();           // writes currentBPM + currentTimeSig back to DB
    void saveProjectName(const juce::String& newName);   // updates Projects.name

    // Project Settings dialog (opened by both the toolbar Edit button and
    // the File-menu > Project > Project Settings item).
    void openProjectSettings();

    // Help
    void showHelpDialog();

    // Undo/Redo
    struct Action
    {
        enum Type { AddPattern, RemovePattern, AddClip, RemoveClip, MoveClip, AddTrack, RemoveTrack };
        Type type;

        // Pattern data
        juce::String patternName;
        int patternId = -1;
        int patternIndex = -1;

        // For RemovePattern undo: the PatternNotes rows that were cascaded out,
        // and the PlacedClips that referenced this pattern. These are re-inserted
        // so undo fully restores the pattern's content and placements.
        struct SavedNote { int pitch; int beat; juce::String lyric; int duration; };
        juce::Array<SavedNote> savedNotes;
        juce::Array<PlacedClip> orphanedClips;

        // Clip data
        PlacedClip clip;
        PlacedClip previousClip;
        int clipIndex = -1;

        // Track data
        juce::String trackName;
        int trackIndex = -1;
        int trackId = -1;
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

    // User type cached at construction time ("educational" or "normal").
    // Drives feature gating (e.g. UTAU voice bank is educational-only).
    // Resolved once via DatabaseManager::getUserType — already safe-downgraded
    // to "normal" if the user is unverified-edu.
    juce::String currentUserType = "normal";

    // ── Vocal Synthesis ──
    VocalSynthEngine vocalSynth;
    juce::AudioSourcePlayer synthPlayer;

    struct FullNote
    {
        int pitch;
        int beat;
        juce::String lyric;
        int duration = 1;   // beats, matches PatternNotes.duration
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

    // ── Help overlay ────────────────────────────────────────────────────────
    //  Themed in-component help panel (replaces the old AlertWindow).
    //  Shown instantly with no system-window creation overhead. Dismissed
    //  by the close button, by clicking outside the card, or Esc.
    //  Has two modes: Help (instructions reference) and About (project
    //  description). Switch via setMode before each setVisible(true).
    class HelpOverlay : public juce::Component
    {
    public:
        enum class Mode { Help, About };

        HelpOverlay();
        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void visibilityChanged() override;

        void setMode(Mode m);
    private:
        juce::Rectangle<int> getCardBounds() const;
        static juce::String  getHelpBody();
        static juce::String  getAboutBody();

        Mode             currentMode = Mode::Help;
        juce::Label      titleLabel;
        juce::TextButton closeButton;
        juce::TextEditor body;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpOverlay)
    };
    HelpOverlay helpOverlay;

    void showAboutDialog();

    // ── Project Settings overlay ────────────────────────────────────────────
    //  Themed card matching HelpOverlay. Hosts project-wide controls:
    //   - Project name (renames the Projects row on commit)
    //   - Master Volume (0-150%, updates VocalSynthEngine::setMasterGain live)
    //   - BPM (30-522, updates VocalSynthEngine::setTempo live + DB)
    //   - Time Signature (popup, updates engine + DB)
    //  Live callbacks let DAWComponent sync its toolbar labels and persist
    //  changes without waiting for the overlay to close.
    class ProjectSettingsOverlay : public juce::Component,
        private juce::Slider::Listener
    {
    public:
        ProjectSettingsOverlay();
        ~ProjectSettingsOverlay() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void visibilityChanged() override;

        // Seed current project values before showing. Call setVisible(true) after.
        void prime(const juce::String& projectName,
            int bpm,
            const juce::String& timeSig,
            float masterVolume01);

        // Owner (DAWComponent) wires these in ctor
        std::function<void(const juce::String&)> onProjectNameCommitted;
        std::function<void(int)>                 onBpmChanged;
        std::function<void(const juce::String&)> onTimeSigChanged;
        std::function<void(float)>               onMasterVolumeChanged;

    private:
        void sliderValueChanged(juce::Slider* s) override;
        juce::Rectangle<int> getCardBounds() const;

        juce::Label      titleLabel;
        juce::TextButton closeButton;

        juce::Label      nameLabel;
        juce::TextEditor nameEditor;

        juce::Label      volumeLabel;
        juce::Label      volumeValueLabel;
        juce::Slider     volumeSlider;

        juce::Label      bpmLabel;
        juce::Label      bpmValueLabel;
        juce::Slider     bpmSlider;

        juce::Label      timeSigLabel;
        juce::TextButton timeSigButton;

        juce::Label      footerLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSettingsOverlay)
    };
    ProjectSettingsOverlay projectSettingsOverlay;

    // Session-local master volume (0.0..1.5). 1.0 = unity. Mirrored on the
    // engine via VocalSynthEngine::setMasterGain on every change.
    float masterVolume01 = 1.0f;

    // Enable/disable transport + inspector buttons based on isVocalBankReady
    void refreshTransportEnabled();

    // ── Voice bank selector (fighting-game-style character select) ──────────
    //
    //  Owned by DAWComponent. Shown as a full-screen modal child when the
    //  "Select" toolbar button is pressed. On select, a background thread
    //  hot-swaps the active voice bank via VocalSynthEngine::reloadVoiceBank.
    //
    //  Folder layout on disk:
    //      Resources/VoiceBank/Aaron/A3/...  Aaron/C4/...  Aaron/F4/...
    //      Resources/VoiceBank/UTAU/A3/...   UTAU/C4/...   UTAU/F4/...
    //  Cards for banks whose folders do not exist are hidden.
    VoiceBankSelectorOverlay voiceBankSelectorOverlay;
    juce::File               voiceBankRoot;     // Resources/VoiceBank
    juce::String             currentVoiceBankId = "aaron";

    void openVoiceBankSelector();
    void onVoiceBankChosen(const juce::String& bankId);
    void onVoiceBankSwapFinished(const juce::String& bankId, bool success);
    juce::Array<VoiceBankSelectorOverlay::BankInfo> discoverAvailableBanks() const;

    class VoiceBankSwapThread : public juce::Thread
    {
    public:
        VoiceBankSwapThread(DAWComponent& owner,
            const juce::File& bankFolder,
            const juce::String& bankId)
            : juce::Thread("VocalNite VoiceBank Swap"),
            owner(owner), bankFolder(bankFolder), bankId(bankId) {
        }

        void run() override;
    private:
        DAWComponent& owner;
        juce::File    bankFolder;
        juce::String  bankId;
    };

    std::unique_ptr<VoiceBankSwapThread> voiceBankSwapThread;

    // ── Timeline export (File menu > Export As) ─────────────────────────────
    //
    //  Renders every PlacedClip on the timeline through VocalSynthEngine
    //  offline (faster than realtime), writes the result as a 16-bit WAV.
    //  Defaults the save location to ~/Downloads (falling back to Documents).
    //
    //  Flow:
    //   1. exportTimelineAsWav() — gates on bank-ready + non-empty timeline,
    //      shows a FileChooser, schedules startExport() on confirmation.
    //   2. startExport() — stops transport, detaches the engine from the audio
    //      device, snapshots clips/notes/BPM, shows the loading overlay, kicks
    //      off ExportThread.
    //   3. ExportThread::run() — offline render loop: walks samples in
    //      blocks, triggers per-beat queueLyric calls (mirroring the live
    //      timer's "integer-beat crossing" logic), pulls audio out via
    //      vocalSynth.getNextAudioBlock(), writes the assembled buffer to
    //      disk through juce::WavAudioFormat, posts onExportFinished via
    //      MessageManager::callAsync.
    //   4. onExportFinished() — restores the engine to live audio (re-prepares
    //      at the device's native sample rate, re-attaches the callback),
    //      hides the overlay, shows a result alert.
    //
    //  All reads of placedClips / patternFullNotes during export are done
    //  against the snapshot inside ExportThread, so the user can keep clicking
    //  in the UI without racing the render. The audio-thread engine isn't
    //  active during the export (callback removed), so the export thread
    //  has exclusive access to vocalSynth.
    void exportTimelineAsWav();
    void startExport(const juce::File& destFile);
    void onExportFinished(bool success, const juce::File& destFile);

    class ExportThread : public juce::Thread
    {
    public:
        ExportThread(DAWComponent& owner,
            const juce::File& destFile,
            juce::Array<PlacedClip> clipsCopy,
            juce::Array<juce::Array<FullNote>> fullNotesCopy,
            int bpmCopy,
            double maxBeatCopy)
            : juce::Thread("VocalNite Export"),
            owner(owner),
            destFile(destFile),
            clipsCopy(std::move(clipsCopy)),
            fullNotesCopy(std::move(fullNotesCopy)),
            bpm(bpmCopy),
            maxBeat(maxBeatCopy) {
        }

        void run() override;

    private:
        // Mirrors DAWComponent::triggerNotesAtBeat but reads from the local
        // snapshot — ensures concurrent UI edits during export can't mutate
        // what we're rendering.
        void triggerForBeat(int globalBeat);

        DAWComponent& owner;
        juce::File    destFile;
        juce::Array<PlacedClip>            clipsCopy;
        juce::Array<juce::Array<FullNote>> fullNotesCopy;
        int    bpm;
        double maxBeat;
    };

    std::unique_ptr<ExportThread> exportThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWComponent)
};