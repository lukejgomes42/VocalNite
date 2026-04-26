#include <JuceHeader.h>
#include "DAWComponent.h"
#include "../Database/DatabaseManager.h"
#include "../Audio/VocalSynthEngine.h"
#include "../Educational/EducationalModeManager.h"
#include "../Educational/TooltipRegistry.h"
#include <libpq-fe.h>
#include <unordered_set>

// ──────────────────────────────────────────────────────────────────────────
//  TransportButtonLookAndFeel
//  Draws crisp vector glyphs (play triangle / pause bars / stop square) on
//  top of a rounded, subtly-gradient-filled button background. We identify
//  which glyph to draw via each button's componentID ("transport_play",
//  "transport_pause", "transport_stop"). This keeps the existing
//  juce::TextButton members intact — all existing setColour, setEnabled,
//  and onClick wiring continues to work unchanged.
//  The LnF reads textColourOnId/textColourOffId so refreshTransportEnabled()
//  dimming still works when the voice bank is still loading.
// ──────────────────────────────────────────────────────────────────────────
class TransportButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& /*backgroundColour*/,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

        juce::Colour base = button.findColour(juce::TextButton::buttonColourId);
        if (shouldDrawButtonAsDown)       base = base.brighter(0.18f);
        else if (shouldDrawButtonAsHighlighted) base = base.brighter(0.10f);

        // Subtle vertical gradient — gives the button a bit of depth
        juce::ColourGradient grad(base.brighter(0.18f),
            bounds.getCentreX(), bounds.getY(),
            base.darker(0.35f),
            bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, 6.0f);

        // Thin border — brighter on hover
        g.setColour(juce::Colour(100, 70, 150).withAlpha(shouldDrawButtonAsHighlighted ? 0.95f : 0.65f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
        bool /*shouldDrawButtonAsHighlighted*/,
        bool shouldDrawButtonAsDown) override
    {
        const auto id = button.getComponentID();
        const auto bounds = button.getLocalBounds().toFloat();

        // Glyph colour follows the button's text colour so the disabled-dim
        // done by refreshTransportEnabled() still works.
        juce::Colour glyph = button.findColour(
            button.getToggleState() ? juce::TextButton::textColourOnId
            : juce::TextButton::textColourOffId);
        if (!button.isEnabled()) glyph = glyph.withAlpha(0.45f);
        if (shouldDrawButtonAsDown) glyph = glyph.brighter(0.10f);

        g.setColour(glyph);

        // Available icon area with padding so glyphs don't touch the border
        auto icon = bounds.reduced(bounds.getWidth() * 0.28f,
            bounds.getHeight() * 0.22f);

        if (id == "transport_play")
        {
            // Rightward-pointing triangle, centered
            juce::Path p;
            p.addTriangle(icon.getX(), icon.getY(),
                icon.getX(), icon.getBottom(),
                icon.getRight(), icon.getCentreY());
            g.fillPath(p);
        }
        else if (id == "transport_pause")
        {
            // Two vertical rounded bars with a small gap
            const float barW = icon.getWidth() * 0.30f;
            const float gap = icon.getWidth() * 0.18f;
            const float cx = icon.getCentreX();
            juce::Rectangle<float> left(cx - gap * 0.5f - barW, icon.getY(), barW, icon.getHeight());
            juce::Rectangle<float> right(cx + gap * 0.5f, icon.getY(), barW, icon.getHeight());
            g.fillRoundedRectangle(left, 1.5f);
            g.fillRoundedRectangle(right, 1.5f);
        }
        else if (id == "transport_stop")
        {
            // Solid slightly-rounded square, centered
            const float side = juce::jmin(icon.getWidth(), icon.getHeight()) * 0.92f;
            auto sq = icon.withSizeKeepingCentre(side, side);
            g.fillRoundedRectangle(sq, 2.0f);
        }
        else
        {
            // Fallback: behave like a normal TextButton if no id matches
            juce::LookAndFeel_V4::drawButtonText(g, button,
                /*isOver*/ false, shouldDrawButtonAsDown);
        }
    }
};

static TransportButtonLookAndFeel& getSharedTransportLnF()
{
    // Function-local static: constructed on first call, destroyed at app exit
    // (well after any DAWComponent teardown). No ordering issues.
    static TransportButtonLookAndFeel lnf;
    return lnf;
}

// ──────────────────────────────────────────────────────────────────────────
//  TopRightTooltipLookAndFeel
//  A LookAndFeel variant that pins tooltips to the top-right corner of the
//  parent window instead of following the cursor. Attached via setLookAndFeel
//  on a dedicated TooltipWindow inside PatternEditorWindow — cursor-tracking
//  tooltips elsewhere in the DAW are unaffected.
// ──────────────────────────────────────────────────────────────────────────
class TopRightTooltipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
        juce::Point<int> screenPos,
        juce::Rectangle<int> parentArea) override
    {
        // Ask the base class to compute the natural size for this text, then
        // ignore its position and anchor top-right with a 12px inset.
        const auto natural = juce::LookAndFeel_V4::getTooltipBounds(tipText, screenPos, parentArea);
        const int w = natural.getWidth();
        const int h = natural.getHeight();

        const int x = parentArea.getRight() - w - 12;
        const int y = parentArea.getY() + 12;

        return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
    }
};

class PatternEditorWindow : public juce::DocumentWindow
{
public:
    PatternEditorWindow(const juce::String& title)
        : DocumentWindow(title, juce::Colour(15, 15, 25), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, true);
        setResizeLimits(800, 500, 3200, 2000);

        tooltipWindow.setLookAndFeel(&tooltipLnF);
    }

    ~PatternEditorWindow() override
    {
        tooltipWindow.setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        delete this;
    }

    // Auto-save & close: when the user clicks back into the main DAW window
    // (or any other app), commit any open lyric edit and dismiss the editor.
    // The PianoRollComponent's lyric editor already commits on focus loss
    // (its onFocusLost handler hits commitCurrentLyricEdit), and per-note
    // edits are persisted as you make them, so simply destroying the window
    // here is a clean save-and-close.
    void activeWindowStatusChanged() override
    {
        // Skip the very first call (window initially gaining focus on creation)
        if (!hasBecomeActiveOnce)
        {
            if (isActiveWindow()) hasBecomeActiveOnce = true;
            return;
        }
        if (isActiveWindow()) return;   // we just regained focus — nothing to do

        // Defer deletion to the next message-loop tick so any pending
        // focus-loss commits on the lyric editor get to fire first.
        juce::Component::SafePointer<PatternEditorWindow> safeThis(this);
        juce::MessageManager::callAsync([safeThis]()
            {
                if (auto* w = safeThis.getComponent())
                    delete w;
            });
    }

private:
    bool hasBecomeActiveOnce = false;

    TopRightTooltipLookAndFeel tooltipLnF;
    juce::TooltipWindow        tooltipWindow{ this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternEditorWindow)
};

DAWComponent::DAWComponent(const juce::String& projectName, int projectId, const juce::String& username)
    : menuBar(this), currentProjectName(projectName), currentProjectId(projectId), currentUsername(username)
{
    // Resolve user type ONCE at construction. getUserType returns "educational"
    // only for verified-edu users — if verification is pending it returns
    // "normal" automatically (safety downgrade). Drives bank gating below.
    if (currentUsername.isNotEmpty())
    {
        try { currentUserType = DatabaseManager::get().getUserType(currentUsername); }
        catch (...) { currentUserType = "normal"; }
    }

    // Menu bar
    addAndMakeVisible(menuBar);

    // Logo placeholder
    logoLabel.setText("VocalNite", juce::dontSendNotification);
    logoLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    logoLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    addAndMakeVisible(logoLabel);

    // Username label
    usernameLabel.setFont(juce::Font(16.0f));
    usernameLabel.setJustificationType(juce::Justification::centredRight);
    usernameLabel.setColour(juce::Label::textColourId, juce::Colour(180, 140, 210));
    addAndMakeVisible(usernameLabel);
    usernameLabel.setText(currentUsername, juce::dontSendNotification);

    // Project name
    projectNameLabel.setText(currentProjectName, juce::dontSendNotification);
    projectNameLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    projectNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    projectNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(projectNameLabel);

    // Transport buttons — glyphs are drawn by TransportButtonLookAndFeel.
    // Each button's componentID tells the LnF which shape to render.
    // Keeping them as TextButton preserves all existing onClick/setColour/
    // setEnabled wiring and HighlightOverlay highlighting.
    playButton.setComponentID("transport_play");
    pauseButton.setComponentID("transport_pause");
    stopButton.setComponentID("transport_stop");

    // Clear any old text (the LnF ignores it, but this keeps a11y labels clean)
    playButton.setButtonText({});
    pauseButton.setButtonText({});
    stopButton.setButtonText({});

    for (auto* btn : { &playButton, &pauseButton, &stopButton })
    {
        btn->setLookAndFeel(&getSharedTransportLnF());
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(48, 30, 72));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    playButton.onClick = [this]()
        {
            // Toggle: pressing Play while already playing pauses instead of doing nothing.
            if (isPlaying)
            {
                // Pause path — same logic as the Pause button
                isPlaying = false;
                stopTimer();
                vocalSynth.setPaused(true);          // freeze audio output
                vocalSynth.setMetronomeEnabled(false);
                if (EducationalModeManager::getInstance().isEnabled())
                    highlightOverlay.highlight(&playButton, juce::Colours::cyan);
                return;
            }

            // Resume/start path
            isPlaying = true;
            vocalSynth.setPaused(false);             // un-freeze; voices continue from where they left off
            startTimer(1000 / 60); // 60 FPS
            if (metronomeEnabled)
            {
                vocalSynth.resetMetronome(playheadPosition);
                vocalSynth.setMetronomeEnabled(true);
            }
            if (EducationalModeManager::getInstance().isEnabled())
                highlightOverlay.highlight(&playButton, juce::Colours::cyan);
        };

    pauseButton.onClick = [this]()
        {
            isPlaying = false;
            stopTimer();
            vocalSynth.setPaused(true);              // freeze audio output
            vocalSynth.setMetronomeEnabled(false);
            if (EducationalModeManager::getInstance().isEnabled())
                highlightOverlay.highlight(&pauseButton, juce::Colours::cyan);
        };

    stopButton.onClick = [this]()
        {
            isPlaying = false;
            stopTimer();
            playheadPosition = 0.0;
            lastTriggeredBeat = -1;
            vocalSynth.setPaused(false);             // unpause so stop() can clear voices
            vocalSynth.stop();
            vocalSynth.setMetronomeEnabled(false);
            if (EducationalModeManager::getInstance().isEnabled())
                highlightOverlay.highlight(&stopButton, juce::Colours::cyan);
            repaint();
        };

    // Mode buttons
    selectModeButton.setButtonText("Select Voice");
    editModeButton.setButtonText("Settings");

    for (auto* btn : { &selectModeButton, &editModeButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0, 60, 120));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    // Select button opens the fighting-game-style voice bank picker.
    selectModeButton.onClick = [this]() { openVoiceBankSelector(); };

    // Edit button opens the Project Settings overlay (same as File > Project > Project Settings).
    editModeButton.onClick = [this]() { openProjectSettings(); };

    // Metronome button
    metronomeButton.setButtonText("Metro");
    metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
    metronomeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    metronomeButton.onClick = [this]()
        {
            metronomeEnabled = !metronomeEnabled;

            // Only engage the audio-thread metronome if we're actually playing
            if (isPlaying)
            {
                if (metronomeEnabled)
                {
                    vocalSynth.resetMetronome(playheadPosition);
                    vocalSynth.setMetronomeEnabled(true);
                }
                else
                {
                    vocalSynth.setMetronomeEnabled(false);
                }
            }

            metronomeButton.setColour(juce::TextButton::buttonColourId,
                metronomeEnabled ? juce::Colour(0, 120, 80) : juce::Colour(40, 40, 60));

            if (EducationalModeManager::getInstance().isEnabled())
                highlightOverlay.highlight(&metronomeButton, juce::Colours::cyan);
        };
    addAndMakeVisible(metronomeButton);

    // Initialize audio device
    audioDeviceManager.initialiseWithDefaultDevices(0, 2);

    // ── Find Resources folder (instant — just directory checks) ─────────────
    juce::File resourcesDir;
    juce::File searchDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    for (int i = 0; i < 10; ++i)
    {
        juce::File candidate = searchDir.getChildFile("Resources");
        if (candidate.isDirectory() && candidate.getChildFile("cmudict.txt").existsAsFile())
        {
            resourcesDir = candidate;
            break;
        }
        searchDir = searchDir.getParentDirectory();
    }

    DBG("Resources dir found: " + resourcesDir.getFullPathName());
    DBG("Dict file exists: " + juce::String(resourcesDir.getChildFile("cmudict.txt").existsAsFile() ? "YES" : "NO"));
    DBG("VoiceBank exists: " + juce::String(resourcesDir.getChildFile("VoiceBank").isDirectory() ? "YES" : "NO"));

    // Remember the VoiceBank root so the character-select overlay and the
    // hot-swap thread can resolve "Aaron" / "UTAU" subfolders without having
    // to rediscover the resources path later.
    voiceBankRoot = resourcesDir.getChildFile("VoiceBank");

    // Hook up the audio callback immediately. If the user hits play before the
    // voice bank is loaded, queueLyric will just be a no-op (findBuffer returns
    // nullptr, buildVoice returns empty). Safe.
    synthPlayer.setSource(&vocalSynth);
    audioDeviceManager.addAudioCallback(&synthPlayer);

    // Sync initial tempo and time signature to the engine
    vocalSynth.setTempo((double)currentBPM);
    vocalSynth.setTimeSignature(4, 4);

    // ── Kick off background resource loading ────────────────────────────────
    // Dictionary (~100ms) + VoiceBank (~3400 WAV files, several seconds) — all
    // on a worker thread. UI stays responsive; transport buttons stay disabled
    // until the voice bank is ready.
    if (resourcesDir != juce::File())
    {
        resourceLoader.reset(new ResourceLoader(*this, resourcesDir));
        resourceLoader->startThread();
    }
    else
    {
        DBG("ERROR: Could not locate Resources folder!");
        loadingOverlay.setStatus("Error: Resources folder not found");
    }

    addChildComponent(pianoRoll);

    // Pattern browser
    addPatternButton.setButtonText("+ Add Pattern");
    addPatternButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 0, 90));
    addPatternButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addPatternButton.onClick = [this]() { addPattern(); };
    addAndMakeVisible(addPatternButton);

    patternRenameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(50, 20, 80));
    patternRenameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    patternRenameEditor.setVisible(false);

    // Commit-and-hide helper: factored out so both onReturnKey and onFocusLost
    // can share the same persistence path. Idempotent — safe to call when no
    // pattern is being edited (just hides the editor and returns).
    auto commitPatternRename = [this]()
        {
            if (editingPatternIndex < 0 || editingPatternIndex >= patternNames.size())
            {
                editingPatternIndex = -1;
                patternRenameEditor.setVisible(false);
                return;
            }

            juce::String newName = patternRenameEditor.getText().trim();
            if (newName.isEmpty())
            {
                // Don't allow blank names — keep the existing name and bail.
                editingPatternIndex = -1;
                patternRenameEditor.setVisible(false);
                repaint();
                return;
            }

            patternNames.set(editingPatternIndex, newName);

            if (editingPatternIndex < patternIds.size() && patternIds[editingPatternIndex] >= 0)
            {
                try
                {
                    std::string patternIdStr = std::to_string(patternIds[editingPatternIndex]);
                    const char* params[2] = { newName.toRawUTF8(), patternIdStr.c_str() };
                    PGresult* res = PQexecParams(DatabaseManager::get().db(),
                        "UPDATE Patterns SET name = $1 WHERE pattern_id = $2",
                        2, nullptr, params, nullptr, nullptr, 0);
                    if (PQresultStatus(res) != PGRES_COMMAND_OK)
                        DBG("Pattern rename error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                    PQclear(res);
                }
                catch (const std::exception& e)
                {
                    DBG("Pattern rename error: " + juce::String(e.what()));
                }
            }

            editingPatternIndex = -1;
            patternRenameEditor.setVisible(false);
            repaint();
        };

    patternRenameEditor.onReturnKey = commitPatternRename;
    patternRenameEditor.onEscapeKey = [this]()
        {
            // Escape: cancel — drop the in-progress edit without saving.
            editingPatternIndex = -1;
            patternRenameEditor.setVisible(false);
            repaint();
        };
    // Click-away save. Mirrors PianoRollComponent's lyric editor: a one-shot
    // suppress flag absorbs spurious focus-loss events fired when we hide
    // the editor programmatically (e.g. opening a new rename session).
    patternRenameEditor.onFocusLost = [this, commitPatternRename]()
        {
            if (suppressNextRenameFocusLost) return;
            commitPatternRename();
        };
    addAndMakeVisible(patternRenameEditor);

    addTrackButton.setButtonText("+ Add Track");
    addTrackButton.setColour(juce::TextButton::buttonColourId, juce::Colour(20, 80, 20));
    addTrackButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addTrackButton.onClick = [this]() { addTrack(); };
    addAndMakeVisible(addTrackButton);

    trackScrollBar.addListener(this);
    addAndMakeVisible(trackScrollBar);
    patternScrollBar.addListener(this);
    addAndMakeVisible(patternScrollBar);
    horizontalScrollBar.addListener(this);
    addAndMakeVisible(horizontalScrollBar);

    // Tempo and time signature
    tempoButton.setButtonText("BPM: 120");
    tempoButton.onClick = [this]()
        {
            auto* dialog = new juce::AlertWindow("Change BPM", "Enter new BPM:", juce::AlertWindow::NoIcon);
            dialog->addTextEditor("bpm", juce::String(currentBPM), "BPM:");
            dialog->addButton("OK", 1);
            dialog->addButton("Cancel", 0);
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, dialog](int result)
                {
                    if (result == 1)
                    {
                        int newBPM = dialog->getTextEditorContents("bpm").getIntValue();
                        if (newBPM > 0 && newBPM <= 522)
                        {
                            currentBPM = newBPM;
                            vocalSynth.setTempo((double)currentBPM);
                            tempoButton.setButtonText("BPM: " + juce::String(currentBPM));
                        }
                    }
                }), true);
        };

    timeSigButton.setButtonText("4/4");
    timeSigButton.onClick = [this]()
        {
            juce::PopupMenu menu;
            juce::StringArray timeSigs = {
                "2/4", "3/4", "4/4", "5/4", "6/4", "7/4",
                "3/8", "5/8", "6/8", "7/8", "9/8", "12/8",
                "2/2", "3/2", "4/2"
            };

            for (int i = 0; i < timeSigs.size(); ++i)
                menu.addItem(i + 1, timeSigs[i]);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(timeSigButton),
                [this, timeSigs](int result)
                {
                    if (result > 0)
                    {
                        currentTimeSig = timeSigs[result - 1];
                        timeSigButton.setButtonText(currentTimeSig);
                        int num = 4, den = 4;
                        parseTimeSignature(currentTimeSig, num, den);
                        vocalSynth.setTimeSignature(num, den);
                    }
                });
        };

    for (auto* btn : { &tempoButton, &timeSigButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(20, 80, 20));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    loadPatterns();
    loadProjectSettings();      // seeds currentBPM + currentTimeSig from Projects row
    loadPatternNotes();
    loadFullPatternNotes();
    loadClips();
    updateClipDurations();   // sync freshly-loaded clips with current pattern sizes
    loadTracks();
    if (trackNames.isEmpty())
    {
        for (int i = 1; i <= 5; ++i)
        {
            juce::String trackName = "Track " + juce::String(i);
            trackNames.add(trackName);
            saveTrack(trackName, i - 1);
        }
    }

    // ── Educational Mode setup ──────────────────────────────────────────────
    addChildComponent(synthInspector);         // owned here, but shown in inspectorWindow when open
    addAndMakeVisible(highlightOverlay);
    highlightOverlay.setAlwaysOnTop(true);
    highlightOverlay.setInterceptsMouseClicks(false, false);

    synthInspector.setSynthEngine(&vocalSynth);

    inspectorToggleButton.setButtonText("Inspector");
    inspectorToggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 80, 120));
    inspectorToggleButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    inspectorToggleButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    inspectorToggleButton.onClick = [this]()
        {
            if (inspectorWindow != nullptr)
                closeInspectorWindow();
            else
                showInspectorPatternPicker();
        };
    addAndMakeVisible(inspectorToggleButton);

    EducationalModeManager::getInstance().addListener(this);
    // Apply current ed-mode state (in case it was enabled before this component was built)
    educationalModeChanged(EducationalModeManager::getInstance().isEnabled());

    // Loading overlay — sits on top of everything until vocal resources are ready
    addAndMakeVisible(loadingOverlay);
    loadingOverlay.setAlwaysOnTop(true);
    loadingOverlay.setStatus("Loading voice bank...");
    refreshTransportEnabled();   // disables transport until voice bank is ready

    // Help overlay — hidden by default, toggled on via Help > VocalNite Help
    addChildComponent(helpOverlay);
    helpOverlay.setAlwaysOnTop(true);

    // Project Settings overlay — hidden by default, shown by toolbar "Edit"
    // button and by File-menu > Project > Project Settings.
    addChildComponent(projectSettingsOverlay);
    projectSettingsOverlay.setAlwaysOnTop(true);

    projectSettingsOverlay.onProjectNameCommitted = [this](juce::String newName)
        {
            newName = newName.trim();
            if (newName.isEmpty() || newName == currentProjectName) return;
            currentProjectName = newName;
            projectNameLabel.setText(newName, juce::dontSendNotification);
            saveProjectName(newName);
        };

    projectSettingsOverlay.onBpmChanged = [this](int newBpm)
        {
            newBpm = juce::jlimit(30, 522, newBpm);
            if (newBpm == currentBPM) return;
            currentBPM = newBpm;
            vocalSynth.setTempo((double)currentBPM);
            tempoButton.setButtonText("BPM: " + juce::String(currentBPM));

            // If the metronome is currently playing, re-align it so the next
            // click uses the new tempo without drifting from the playhead.
            if (isPlaying && metronomeEnabled)
                vocalSynth.resetMetronome(playheadPosition);

            saveProjectSettings();
        };

    projectSettingsOverlay.onTimeSigChanged = [this](juce::String newSig)
        {
            if (newSig.isEmpty() || newSig == currentTimeSig) return;
            currentTimeSig = newSig;
            timeSigButton.setButtonText(currentTimeSig);
            int num = 4, den = 4;
            parseTimeSignature(currentTimeSig, num, den);
            vocalSynth.setTimeSignature(num, den);

            if (isPlaying && metronomeEnabled)
                vocalSynth.resetMetronome(playheadPosition);

            saveProjectSettings();
        };

    projectSettingsOverlay.onMasterVolumeChanged = [this](float newVol01)
        {
            masterVolume01 = juce::jlimit(0.0f, 1.5f, newVol01);
            vocalSynth.setMasterGain(masterVolume01);
            saveProjectSettings();
        };

    // Voice bank selector — hidden by default, shown via the toolbar "Select" button.
    addChildComponent(voiceBankSelectorOverlay);
    voiceBankSelectorOverlay.setAlwaysOnTop(true);
    voiceBankSelectorOverlay.onBankSelected = [this](juce::String id)
        {
            if (!isDying.load())
                onVoiceBankChosen(id);
        };

    setSize(1280, 720);
}

DAWComponent::~DAWComponent()
{
    // Signal any in-flight async callbacks that the component is going away.
    // They check this before touching any member state.
    isDying.store(true);

    // Drop LookAndFeel references before our LnF singleton (if ever rebuilt
    // or replaced) can be touched. These buttons are TextButton members that
    // pointed at TransportButtonLookAndFeel via setLookAndFeel().
    playButton.setLookAndFeel(nullptr);
    pauseButton.setLookAndFeel(nullptr);
    stopButton.setLookAndFeel(nullptr);

    // Stop the resource loader first — if it's still running, it needs to
    // finish before we start tearing down other members it might touch.
    if (resourceLoader != nullptr)
    {
        resourceLoader->stopThread(5000);  // wait up to 5s
        resourceLoader.reset();
    }

    // ── Stop the export thread (if a render is in flight) ───────────────────
    // Mirrors the voiceBankSwapThread cleanup below. isDying was set above so
    // any in-flight ExportThread::run() will see threadShouldExit() and bail.
    if (exportThread != nullptr)
    {
        exportThread->stopThread(5000);
        exportThread.reset();
    }

    // Stop any in-flight voice bank hot-swap too.
    if (voiceBankSwapThread != nullptr)
    {
        voiceBankSwapThread->stopThread(5000);
        voiceBankSwapThread.reset();
    }

    EducationalModeManager::getInstance().removeListener(this);

    if (inspectorWindow != nullptr)
    {
        delete inspectorWindow;
        inspectorWindow = nullptr;
    }

    audioDeviceManager.removeAudioCallback(&synthPlayer);
    synthPlayer.setSource(nullptr);
}

void DAWComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(15, 15, 25));

    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int patternWidth = 150;
    int trackHeaderWidth = 80;
    int gridLeft = patternWidth + trackHeaderWidth;
    int cellWidth = (int)(80.0f * cellWidthMultiplier);
    int scrollBarWidth = 12;
    int gridWidth = getWidth() - gridLeft - scrollBarWidth;
    int scrolledX = (int)horizontalScrollOffset;

    // Toolbar backgrounds
    g.setColour(juce::Colour(30, 10, 50));
    g.fillRect(0, menuBarHeight, getWidth(), toolbarHeight);
    g.setColour(juce::Colour(20, 20, 40));
    g.fillRect(0, menuBarHeight + toolbarHeight, getWidth(), toolbar2Height);

    // Metronome visual flash
    if (metronomeEnabled && metronomeBeat)
    {
        g.setColour(juce::Colours::limegreen.withAlpha(0.6f));
        g.fillRoundedRectangle(170 + (28 + 4) * 3, menuBarHeight + toolbarHeight + 3, 50, 28, 4.0f);
    }

    // Pattern browser background
    g.setColour(juce::Colour(20, 10, 35));
    g.fillRect(0, gridTop, patternWidth, getHeight() - gridTop);

    // Section off buttons area at bottom
    g.setColour(juce::Colour(30, 15, 50));
    g.fillRect(0, getHeight() - 70, patternWidth, 70);
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(0, getHeight() - 70, patternWidth, getHeight() - 70, 1.0f);

    // Track header background
    g.setColour(juce::Colour(35, 15, 55));
    g.fillRect(patternWidth, gridTop, trackHeaderWidth, getHeight() - gridTop);
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(gridLeft, gridTop, gridLeft, getHeight(), 1.0f);

    // Measure numbers header
    g.setColour(juce::Colour(30, 30, 50));
    g.fillRect(gridLeft, gridTop, gridWidth, 20);
    g.setColour(juce::Colours::grey);
    g.setFont(12.0f);
    int numCols = gridWidth / cellWidth + 1;
    int startCol = scrolledX / cellWidth;
    for (int col = startCol; col < startCol + numCols + 1; ++col)
    {
        int x = gridLeft + col * cellWidth - scrolledX;
        if (x > getWidth()) break;
        g.drawText(juce::String(col + 1), x + 4, gridTop, cellWidth, 20, juce::Justification::centredLeft);
        // Draw tick marks
        g.setColour(juce::Colour(80, 80, 120));
        g.drawLine(x, gridTop + 14, x, gridTop + 20, 1.0f);
        g.setColour(juce::Colours::grey);
    }

    int trackAreaTop = gridTop + 20;

    // Draw dynamic tracks
    for (int i = 0; i < trackNames.size(); ++i)
    {
        int y = trackAreaTop + i * trackHeight - (int)trackScrollOffset;

        if (y + trackHeight < trackAreaTop || y > getHeight()) continue;

        // Track row background - only in grid area
        g.setColour(i % 2 == 0 ? juce::Colour(25, 25, 40) : juce::Colour(20, 20, 35));
        g.fillRect(gridLeft, y, gridWidth, trackHeight);

        // Vertical beat lines
        g.setColour(juce::Colour(60, 60, 90));
        for (int col = startCol; col <= startCol + numCols + 1; ++col)
        {
            int x = gridLeft + col * cellWidth - scrolledX;
            if (x > getWidth()) break;
            g.drawLine(x, y, x, y + trackHeight, 1.0f);
        }

        // Remove button in track header
        g.setColour(juce::Colour(150, 30, 30));
        g.fillRoundedRectangle(patternWidth + 4, y + 10, 20, 20, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("-", patternWidth + 4, y + 10, 20, 20, juce::Justification::centred);

        // Track name
        g.setFont(12.0f);
        g.drawText(trackNames[i], patternWidth + 26, y, trackHeaderWidth - 28, trackHeight, juce::Justification::centredLeft);

        // Draw placed clips for this track
        for (auto& clip : placedClips)
        {
            if (clip.trackIndex == i)
            {
                int clipX = gridLeft + (int)(clip.startBeat * cellWidth) - scrolledX;
                int clipW = (int)(clip.duration * cellWidth);
                if (clipX + clipW < gridLeft || clipX > getWidth()) continue;
                g.setColour(juce::Colour(100, 40, 160));
                g.fillRoundedRectangle(clipX + 1, y + 2, clipW - 2, trackHeight - 4, 4.0f);
                // Draw note preview as horizontal bars (mini piano roll)
                int pIdx = clip.patternIndex;
                if (pIdx < patternNotePreviews.size())
                {
                    auto& notes = patternNotePreviews.getReference(pIdx);
                    int maxBeat = (int)(clip.duration * 4);
                    if (maxBeat < 1) maxBeat = 1;
                    int minPitch = 127, maxPitch = 0;

                    // Find pitch range for better scaling
                    for (auto& note : notes)
                    {
                        minPitch = std::min(minPitch, note.pitch);
                        maxPitch = std::max(maxPitch, note.pitch);
                    }
                    int pitchRange = std::max(maxPitch - minPitch, 12); // min range of 12 semitones

                    int innerTop = y + 14;           // leave room for label
                    int innerBottom = y + trackHeight - 3;
                    int innerHeight = innerBottom - innerTop;
                    int barH = std::max(2, innerHeight / pitchRange);

                    for (auto& note : notes)
                    {
                        float noteXRatio = (float)note.beat / maxBeat;
                        float noteYRatio = 1.0f - (float)(note.pitch - minPitch) / pitchRange; // flip: high pitch = top
                        int nx = clipX + 2 + (int)(noteXRatio * (clipW - 4));
                        int ny = innerTop + (int)(noteYRatio * (innerHeight - barH));
                        int barW = (int)((float)note.duration / maxBeat * (clipW - 4));
                        if (barW < 3) barW = 3;

                        if (nx >= clipX && nx + barW <= clipX + clipW)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.55f));
                            g.fillRect(nx, ny, barW, barH);
                        }
                    }
                }

                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.setFont(11.0f);
                g.drawText(patternNames[clip.patternIndex], clipX + 4, y + 2, clipW - 8, trackHeight - 4, juce::Justification::centredLeft);
                g.setColour(juce::Colour(140, 60, 200));
                g.drawRoundedRectangle(clipX + 1, y + 2, clipW - 2, trackHeight - 4, 4.0f, 1.0f);
            }
        }

        // Grid lines
        g.setColour(juce::Colour(60, 60, 90));
        g.drawLine(gridLeft, y + trackHeight, gridLeft + gridWidth, y + trackHeight, 1.0f);
    }


    // Redraw left columns on top of everything
    g.setColour(juce::Colour(20, 10, 35));
    g.fillRect(0, gridTop, patternWidth, getHeight() - gridTop);
    g.setColour(juce::Colour(35, 15, 55));
    g.fillRect(patternWidth, gridTop, trackHeaderWidth, getHeight() - gridTop);
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(patternWidth, gridTop, patternWidth, getHeight(), 1.0f);
    g.drawLine(gridLeft, gridTop, gridLeft, getHeight(), 1.0f);

    // Redraw pattern browser content on top
    g.setColour(juce::Colours::grey);
    g.setFont(12.0f);
    g.drawText("PATTERNS", 4, gridTop + 4, patternWidth - 8, 20, juce::Justification::centred);
    drawPatternBrowser(g, gridTop, patternWidth);

    // Redraw track labels on top
    for (int i = 0; i < trackNames.size(); ++i)
    {
        int y = gridTop + 20 + i * trackHeight - (int)trackScrollOffset;
        if (y + trackHeight < gridTop + 20 || y > getHeight()) continue;
        g.setColour(juce::Colour(150, 30, 30));
        g.fillRoundedRectangle(patternWidth + 4, y + 10, 20, 20, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("-", patternWidth + 4, y + 10, 20, 20, juce::Justification::centred);
        g.setFont(12.0f);
        g.drawText(trackNames[i], patternWidth + 26, y, trackHeaderWidth - 28, trackHeight, juce::Justification::centredLeft);
    }

    // Section divider for buttons
    g.setColour(juce::Colour(30, 15, 50));
    g.fillRect(0, getHeight() - 70, patternWidth, 70);
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(0, getHeight() - 70, patternWidth, getHeight() - 70, 1.0f);

    // Playhead
    int playheadX = gridLeft + (int)(playheadPosition * cellWidth) - scrolledX;
    if (playheadX >= gridLeft && playheadX <= getWidth())
    {
        g.setColour(juce::Colours::red);
        g.drawLine(playheadX, gridTop, playheadX, getHeight(), 2.0f);
        // Playhead triangle indicator
        juce::Path triangle;
        triangle.addTriangle(playheadX - 6, gridTop, playheadX + 6, gridTop, playheadX, gridTop + 12);
        g.fillPath(triangle);
    }

    // Drag preview
    if (isDraggingPattern && draggingPatternIndex >= 0)
    {
        g.setColour(juce::Colour(100, 40, 160).withAlpha(0.7f));
        g.fillRoundedRectangle(dragX, dragY - patternHeight / 2, 150, patternHeight - 4, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(12.0f);
        g.drawText(patternNames[draggingPatternIndex], dragX + 4, dragY - patternHeight / 2, 140, patternHeight - 4, juce::Justification::centredLeft);
    }
    else if (isDraggingClip && draggingClipIndex >= 0 && draggingClipIndex < placedClips.size())
    {
        auto& clip = placedClips.getReference(draggingClipIndex);
        int clipW = (int)(clip.duration * cellWidth);
        g.setColour(juce::Colour(100, 40, 160).withAlpha(0.7f));
        g.fillRoundedRectangle(dragX, dragY - trackHeight / 2, clipW, trackHeight - 4, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(11.0f);
        g.drawText(patternNames[clip.patternIndex], dragX + 4, dragY - trackHeight / 2, clipW - 8, trackHeight - 4, juce::Justification::centredLeft);
    }
}

void DAWComponent::resized()
{
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int y1 = menuBarHeight;
    int y2 = y1 + toolbarHeight;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int scrollBarWidth = 12;

    menuBar.setBounds(0, 0, getWidth(), menuBarHeight);

    // Toolbar 1
    logoLabel.setBounds(4, y1 + 5, 100, 30);
    projectNameLabel.setBounds(getWidth() / 2 - 100, y1 + 5, 200, 30);
    selectModeButton.setBounds(getWidth() - 305, y1 + 5, 95, 30);
    editModeButton.setBounds(getWidth() - 205, y1 + 5, 80, 30);
    usernameLabel.setBounds(getWidth() - 120, y1 + 5, 115, 30);

    // Toolbar 2
    tempoButton.setBounds(4, y2 + 4, 90, 26);
    timeSigButton.setBounds(100, y2 + 4, 60, 26);

    int btnSize = 28;
    int btnY = y2 + 3;
    int btnStartX = 170;
    playButton.setBounds(btnStartX, btnY, btnSize, btnSize);
    pauseButton.setBounds(btnStartX + btnSize + 4, btnY, btnSize, btnSize);
    stopButton.setBounds(btnStartX + (btnSize + 4) * 2, btnY, btnSize, btnSize);
    metronomeButton.setBounds(btnStartX + (btnSize + 4) * 3, btnY, 50, btnSize);

    // Add track button at bottom left
    addTrackButton.setBounds(4, getHeight() - 30, 120, 24);

    // Track scrollbar on the right
    int pianoRollHeight = pianoRollVisible ? 250 : 0;
    int trackAreaHeight = getHeight() - gridTop - pianoRollHeight - 30;
    trackScrollBar.setBounds(getWidth() - scrollBarWidth, gridTop, scrollBarWidth, trackAreaHeight);

    int totalTrackHeight = trackNames.size() * trackHeight;
    trackScrollBar.setRangeLimits(0.0, totalTrackHeight);
    trackScrollBar.setCurrentRange(trackScrollOffset, trackAreaHeight);

    // Piano roll
    if (pianoRollVisible)
        pianoRoll.setBounds(0, getHeight() - pianoRollHeight - 30, getWidth(), pianoRollHeight);

    // Pattern browser buttons
    addPatternButton.setBounds(4, getHeight() - 60, 140, 24);

    // Pattern scrollbar
    int patternWidth = 150;
    int patternAreaHeight = getHeight() - gridTop - 60;
    patternScrollBar.setBounds(patternWidth - 10, gridTop + 20, 10, patternAreaHeight);
    int totalPatternHeight = patternNames.size() * patternHeight + patternHeight;
    patternScrollBar.setRangeLimits(0.0, totalPatternHeight);
    patternScrollBar.setCurrentRange(patternScrollOffset, patternAreaHeight);

    // Horizontal scrollbar for grid
    int gridLeft = patternWidth + 80;
    int horizontalScrollBarY = getHeight() - 30;
    horizontalScrollBar.setBounds(gridLeft, horizontalScrollBarY, getWidth() - gridLeft - scrollBarWidth, 12);
    int cellWidth = (int)(80.0f * cellWidthMultiplier);
    int totalGridWidth = 128 * cellWidth;
    horizontalScrollBar.setRangeLimits(0.0, totalGridWidth);
    horizontalScrollBar.setCurrentRange(horizontalScrollOffset, getWidth() - gridLeft);

    // ── Educational: inspector toggle + highlight overlay ───────────────────
    // Inspector toggle sits in the pattern browser footer, above "+ Add Pattern"
    // when educational mode is on.
    if (EducationalModeManager::getInstance().isEnabled())
    {
        inspectorToggleButton.setBounds(4, getHeight() - 90, 140, 24);
        // Push + Add Pattern up so it doesn't overlap
        addPatternButton.setBounds(4, getHeight() - 60, 140, 24);
    }
    else
    {
        inspectorToggleButton.setBounds(0, 0, 0, 0);
    }

    // Overlay always covers the full component (non-interactive)
    highlightOverlay.setBounds(getLocalBounds());
    highlightOverlay.toFront(false);

    // Loading overlay covers the full component while visible (intercepts clicks)
    loadingOverlay.setBounds(getLocalBounds());
    if (loadingOverlay.isVisible())
        loadingOverlay.toFront(false);

    // Help overlay also spans the full area when visible
    helpOverlay.setBounds(getLocalBounds());
    if (helpOverlay.isVisible())
        helpOverlay.toFront(false);

    // Project Settings overlay spans the full area when visible
    projectSettingsOverlay.setBounds(getLocalBounds());
    if (projectSettingsOverlay.isVisible())
        projectSettingsOverlay.toFront(false);

    // Voice-bank selector overlay spans the full area when visible
    voiceBankSelectorOverlay.setBounds(getLocalBounds());
    if (voiceBankSelectorOverlay.isVisible())
        voiceBankSelectorOverlay.toFront(false);
}

juce::StringArray DAWComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Project", "Play", "Tools", "Help" };
}

juce::PopupMenu DAWComponent::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    if (menuIndex == 0) // File
    {
        menu.addItem(3, "Save Project");
        menu.addItem(14, "Export As");
        menu.addSeparator();
        menu.addItem(4, "Dashboard");
    }
    else if (menuIndex == 1) // Edit
    {
        menu.addItem(5, "Undo");
        menu.addItem(6, "Redo");
    }
    else if (menuIndex == 2) // View
    {
        menu.addItem(7, "Zoom In");
        menu.addItem(8, "Zoom Out");
    }
    else if (menuIndex == 3) // Project
    {
        menu.addItem(9, "Project Settings");
    }
    else if (menuIndex == 4) // Play
    {
        menu.addItem(10, "Play");
        menu.addItem(11, "Pause");
        menu.addItem(12, "Stop");
    }
    else if (menuIndex == 5) // Tools
    {
        menu.addItem(13, "Metronome");
    }
    else if (menuIndex == 6) // Help
    {
        menu.addItem(15, "VocalNite Help");
        menu.addSeparator();
        menu.addItem(16, "About VocalNite");
    }
    return menu;
}

void DAWComponent::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
    case 4: // Dashboard
        if (onReturnToDashboard)
            onReturnToDashboard();
        break;

    case 5: // Undo
        performUndo();
        break;

    case 6: // Redo
        performRedo();
        break;

    case 7: // Zoom In
        cellWidthMultiplier = std::min(cellWidthMultiplier + 0.25f, 3.0f);
        repaint();
        break;

    case 8: // Zoom Out
        cellWidthMultiplier = std::max(cellWidthMultiplier - 0.25f, 0.5f);
        repaint();
        break;

    case 9: // Project Settings
        openProjectSettings();
        break;

    case 10: // Play (toggles: pressing while playing = pause)
        if (isPlaying)
        {
            isPlaying = false;
            stopTimer();
            vocalSynth.setPaused(true);
            vocalSynth.setMetronomeEnabled(false);
        }
        else
        {
            isPlaying = true;
            vocalSynth.setPaused(false);
            startTimer(1000 / 60);
            if (metronomeEnabled)
            {
                vocalSynth.resetMetronome(playheadPosition);
                vocalSynth.setMetronomeEnabled(true);
            }
        }
        break;

    case 11: // Pause
        isPlaying = false;
        stopTimer();
        vocalSynth.setPaused(true);
        vocalSynth.setMetronomeEnabled(false);
        break;

    case 12: // Stop
        isPlaying = false;
        stopTimer();
        playheadPosition = 0.0;
        lastTriggeredBeat = -1;
        vocalSynth.setPaused(false);
        vocalSynth.stop();
        vocalSynth.setMetronomeEnabled(false);
        repaint();
        break;

    case 13: // Metronome
        metronomeEnabled = !metronomeEnabled;
        if (isPlaying)
        {
            if (metronomeEnabled)
            {
                vocalSynth.resetMetronome(playheadPosition);
                vocalSynth.setMetronomeEnabled(true);
            }
            else
            {
                vocalSynth.setMetronomeEnabled(false);
            }
        }
        metronomeButton.setColour(juce::TextButton::buttonColourId,
            metronomeEnabled ? juce::Colour(0, 120, 80) : juce::Colour(40, 40, 60));
        break;

    case 14: // Export As
        exportTimelineAsWav();
        break;

    case 15: // Help
        showHelpDialog();
        break;

    case 16: // About VocalNite
        showAboutDialog();
        break;

    default:
        break;
    }
}

void DAWComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &trackScrollBar)
    {
        trackScrollOffset = newRangeStart;
        repaint();
    }
    else if (bar == &patternScrollBar)
    {
        patternScrollOffset = newRangeStart;
        repaint();
    }
    else if (bar == &horizontalScrollBar)
    {
        horizontalScrollOffset = newRangeStart;
        repaint();
    }
}

void DAWComponent::addTrack()
{
    juce::String newName = "Track " + juce::String(trackNames.size() + 1);
    trackNames.add(newName);
    saveTrack(newName, trackNames.size() - 1);   // saveTrack appends to trackIds

    Action action;
    action.type = Action::AddTrack;
    action.trackName = newName;
    action.trackIndex = trackNames.size() - 1;
    action.trackId = trackIds.getLast();
    undoStack.add(action);
    redoStack.clear();
    if (undoStack.size() > 10) undoStack.remove(0);

    int totalHeight = trackNames.size() * trackHeight;
    trackScrollBar.setRangeLimits(0.0, totalHeight);
    resized();
    repaint();
}

void DAWComponent::removeTrack(int index)
{
    if (trackNames.size() > 1 && index >= 0 && index < trackNames.size())
    {
        Action action;
        action.type = Action::RemoveTrack;
        action.trackName = trackNames[index];
        action.trackIndex = index;
        action.trackId = (index < trackIds.size()) ? trackIds[index] : -1;
        undoStack.add(action);
        redoStack.clear();
        if (undoStack.size() > 10) undoStack.remove(0);

        if (index < trackIds.size())
            deleteTrackFromDB(trackIds[index]);

        trackNames.remove(index);
        trackIds.remove(index);
        int totalHeight = trackNames.size() * trackHeight;
        trackScrollBar.setRangeLimits(0.0, totalHeight);
        resized();
        repaint();
    }
}

// ============================================================================
//  getTooltip
//  juce::TooltipClient hook — supplies dynamic hover tooltips for areas of
//  the DAW that aren't separate components. Static-control tooltips
//  (transport buttons, BPM, Inspector, etc.) come from setTooltip on each
//  child component; this function fires only when JUCE walks up the
//  hierarchy and lands on the DAWComponent itself (i.e. cursor is in the
//  pattern browser background, the timeline grid, or the track strip).
// ============================================================================

juce::String DAWComponent::getTooltip()
{
    // Translate the mouse-relative-to-screen back to our local coords.
    const auto screen = juce::Desktop::getMousePosition();
    const auto local = getLocalPoint(nullptr, screen);

    if (!getLocalBounds().contains(local)) return {};

    // ── Layout constants — keep these in sync with paint() / resized() ────
    constexpr int menuBarHeight = 25;
    constexpr int toolbarHeight = 40;
    constexpr int toolbar2Height = 35;
    constexpr int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    constexpr int patternAreaTop = gridTop + 28;
    constexpr int trackAreaTop = gridTop + 20;
    constexpr int patternWidth = 150;
    constexpr int trackHeaderWidth = 80;
    constexpr int gridLeft = patternWidth + trackHeaderWidth;

    // Hovering a pattern row in the browser?
    if (local.x >= 4 && local.x < patternWidth - 4 && local.y >= patternAreaTop)
    {
        for (int i = 0; i < patternNames.size(); ++i)
        {
            const int rowY = patternAreaTop + i * patternHeight - (int)patternScrollOffset;
            juce::Rectangle<int> rowRect(4, rowY, patternWidth - 8, patternHeight - 4);
            if (rowRect.contains(local))
            {
                return juce::String(juce::CharPointer_UTF8(
                    "PATTERN \xe2\x80\x94 Double-click to open the piano roll and edit notes / lyrics. "
                    "Drag onto a track to place a clip on the timeline. "
                    "Right-click for rename, copy, or delete."));
            }
        }
    }

    // Hovering a placed clip in the timeline?
    if (local.x >= gridLeft && local.y >= trackAreaTop)
    {
        const int cellWidth = (int)(80.0f * cellWidthMultiplier);
        for (const auto& clip : placedClips)
        {
            const int trackY = trackAreaTop + clip.trackIndex * trackHeight - (int)trackScrollOffset;
            const int clipX = gridLeft + (int)(clip.startBeat * cellWidth) - (int)horizontalScrollOffset;
            const int clipW = (int)(clip.duration * cellWidth);
            juce::Rectangle<int> clipRect(clipX, trackY, clipW, trackHeight);
            if (clipRect.contains(local))
            {
                return juce::String(juce::CharPointer_UTF8(
                    "CLIP \xe2\x80\x94 Drag horizontally to move along the timeline, drag vertically to "
                    "switch tracks, or drag back into the pattern browser to delete. "
                    "Right-click for clip options."));
            }
        }
    }

    return {};
}

void DAWComponent::mouseDown(const juce::MouseEvent& e)
{
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int trackAreaTop = gridTop + 20;
    int patternAreaTop = gridTop + 28;
    int gridLeft = 200;

    if (e.mods.isRightButtonDown())
    {
        // Right click on pattern
        for (int i = 0; i < patternNames.size(); ++i)
        {
            int y = patternAreaTop + i * patternHeight - (int)patternScrollOffset;
            juce::Rectangle<int> patternRect(4, y, 150 - 8, patternHeight - 4);
            if (patternRect.contains(e.x, e.y))
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Rename");
                menu.addItem(2, "Copy");
                menu.addSeparator();
                menu.addItem(3, "Delete");

                menu.showMenuAsync(juce::PopupMenu::Options(),
                    [this, i](int result)
                    {
                        if (result == 1)
                        {
                            // Rename
                            int menuBarHeight = 25;
                            int toolbarHeight = 40;
                            int toolbar2Height = 35;
                            int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
                            int patternAreaTop = gridTop + 28;
                            int gridLeft = 200;
                            int y = patternAreaTop + i * patternHeight;

                            // Suppress one focus-lost: showing/refocusing the
                            // editor causes the previously-focused window to
                            // emit a focus-loss that would re-enter commit.
                            suppressNextRenameFocusLost = true;
                            juce::MessageManager::callAsync(
                                [this]() { suppressNextRenameFocusLost = false; });

                            editingPatternIndex = i;
                            patternRenameEditor.setText(patternNames[i]);
                            patternRenameEditor.setBounds(4, y, gridLeft - 8, patternHeight - 4);
                            patternRenameEditor.setVisible(true);
                            patternRenameEditor.grabKeyboardFocus();
                            patternRenameEditor.selectAll();
                        }
                        else if (result == 2)
                        {
                            juce::String baseName = patternNames[i];
                            int copyNumber = 1;
                            juce::String copyName = baseName + "(" + juce::String(copyNumber) + ")";

                            while (patternNames.contains(copyName))
                            {
                                copyNumber++;
                                copyName = baseName + "(" + juce::String(copyNumber) + ")";
                            }

                            int newPatternId = -1;

                            if (currentProjectId >= 0)
                            {
                                std::string projectIdStr = std::to_string(currentProjectId);
                                const char* insertParams[2] = { projectIdStr.c_str(), copyName.toRawUTF8() };
                                PGresult* insertRes = PQexecParams(DatabaseManager::get().db(),
                                    "INSERT INTO Patterns (project_id, name) VALUES ($1, $2) RETURNING pattern_id",
                                    2, nullptr, insertParams, nullptr, nullptr, 0);

                                if (PQresultStatus(insertRes) == PGRES_TUPLES_OK)
                                {
                                    newPatternId = std::stoi(PQgetvalue(insertRes, 0, 0));

                                    if (i < patternIds.size() && patternIds[i] >= 0)
                                    {
                                        std::string newIdStr = std::to_string(newPatternId);
                                        std::string oldIdStr = std::to_string(patternIds[i]);
                                        const char* noteParams[2] = { newIdStr.c_str(), oldIdStr.c_str() };
                                        PGresult* notesRes = PQexecParams(DatabaseManager::get().db(),
                                            "INSERT INTO PatternNotes (pattern_id, pitch, beat, lyric, duration) "
                                            "SELECT $1::integer, pitch, beat, lyric, duration FROM PatternNotes WHERE pattern_id = $2",
                                            2, nullptr, noteParams, nullptr, nullptr, 0);
                                        if (PQresultStatus(notesRes) != PGRES_COMMAND_OK)
                                            DBG("Pattern copy notes error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                                        PQclear(notesRes);
                                    }
                                }
                                else
                                {
                                    DBG("Pattern copy error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                                }
                                PQclear(insertRes);
                            }

                            patternNames.add(copyName);
                            patternIds.add(newPatternId);
                            loadPatternNotes();
                            loadFullPatternNotes();
                            repaint();
                            resized();
                        }
                        else if (result == 3)
                        {
                            // Block deleting the pattern currently being inspected
                            if (isInspecting() && i == inspectedPatternIndex)
                            {
                                DBG("Delete blocked: pattern is currently being inspected");
                                return;
                            }

                            Action action;
                            action.type = Action::RemovePattern;
                            action.patternName = patternNames[i];
                            action.patternId = patternIds[i];
                            action.patternIndex = i;

                            // Capture all PatternNotes before DB cascade wipes them
                            if (i < patternIds.size() && patternIds[i] >= 0)
                            {
                                std::string patternIdStr = std::to_string(patternIds[i]);
                                const char* noteParams[1] = { patternIdStr.c_str() };
                                PGresult* notesRes = PQexecParams(DatabaseManager::get().db(),
                                    "SELECT pitch, beat, lyric, duration FROM PatternNotes WHERE pattern_id = $1",
                                    1, nullptr, noteParams, nullptr, nullptr, 0);
                                if (PQresultStatus(notesRes) == PGRES_TUPLES_OK)
                                {
                                    int n = PQntuples(notesRes);
                                    for (int row = 0; row < n; ++row)
                                    {
                                        Action::SavedNote sn;
                                        sn.pitch = std::stoi(PQgetvalue(notesRes, row, 0));
                                        sn.beat = std::stoi(PQgetvalue(notesRes, row, 1));
                                        sn.lyric = juce::String(PQgetvalue(notesRes, row, 2));
                                        sn.duration = std::max(1, std::stoi(PQgetvalue(notesRes, row, 3)));
                                        action.savedNotes.add(sn);
                                    }
                                }
                                PQclear(notesRes);
                            }

                            // Capture placed clips referencing this pattern (they'll be
                            // cascaded out of PlacedClips when we delete the pattern).
                            for (int c = placedClips.size() - 1; c >= 0; --c)
                                if (placedClips[c].patternIndex == i)
                                    action.orphanedClips.add(placedClips[c]);

                            // Delete pattern row (DB cascade wipes PatternNotes + PlacedClips)
                            if (i < patternIds.size() && patternIds[i] >= 0)
                            {
                                try
                                {
                                    std::string patternIdStr = std::to_string(patternIds[i]);
                                    const char* params[1] = { patternIdStr.c_str() };
                                    PGresult* res = PQexecParams(DatabaseManager::get().db(),
                                        "DELETE FROM Patterns WHERE pattern_id = $1",
                                        1, nullptr, params, nullptr, nullptr, 0);
                                    if (PQresultStatus(res) != PGRES_COMMAND_OK)
                                        DBG("Pattern delete error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                                    PQclear(res);
                                }
                                catch (const std::exception& e)
                                {
                                    DBG("Pattern delete error: " + juce::String(e.what()));
                                }
                            }

                            undoStack.add(action);
                            redoStack.clear();
                            if (undoStack.size() > 10) undoStack.remove(0);

                            // Drop clips that referenced this pattern, shift later clips' patternIndex down
                            for (int c = placedClips.size() - 1; c >= 0; --c)
                            {
                                if (placedClips[c].patternIndex == i)
                                    placedClips.remove(c);
                                else if (placedClips[c].patternIndex > i)
                                    placedClips.getReference(c).patternIndex--;
                            }

                            patternNames.remove(i);
                            patternIds.remove(i);
                            // If the inspected pattern came after this one, its index shifts down
                            if (isInspecting() && inspectedPatternIndex > i)
                                inspectedPatternIndex--;
                            loadPatternNotes();
                            loadFullPatternNotes();
                            repaint();
                            resized();
                        }
                    });
                return;
            }
        }

        // ── Right-click on a placed clip in the timeline ────────────────────
        // Hit-test mirrors mouseDrag's clip detection. Menu offers Open
        // Pattern Editor (jumps to the pattern's piano roll), Move to Track
        // (submenu of all tracks), and Delete Clip (with full undo/redo).
        {
            const int cellWidth = (int)(80.0f * cellWidthMultiplier);
            for (int ci = 0; ci < placedClips.size(); ++ci)
            {
                const auto& clip = placedClips.getReference(ci);
                const int trackY = trackAreaTop + clip.trackIndex * trackHeight - (int)trackScrollOffset;
                const int clipX = gridLeft + (int)(clip.startBeat * cellWidth) - (int)horizontalScrollOffset;
                const int clipW = (int)(clip.duration * cellWidth);
                juce::Rectangle<int> clipRect(clipX, trackY, clipW, trackHeight);
                if (!clipRect.contains(e.x, e.y)) continue;

                juce::PopupMenu menu;
                menu.addItem(101, "Open Pattern Editor");
                menu.addSeparator();

                // Move-to-track submenu — disable the row that the clip is
                // already on so the user can't no-op it.
                juce::PopupMenu trackSub;
                for (int t = 0; t < trackNames.size(); ++t)
                {
                    trackSub.addItem(200 + t,
                        trackNames[t],
                        /*isActive*/ t != clip.trackIndex,
                        /*isTicked*/ t == clip.trackIndex);
                }
                menu.addSubMenu("Move to Track", trackSub,
                    /*isActive*/ trackNames.size() > 1);
                menu.addSeparator();
                menu.addItem(102, "Delete Clip");

                menu.showMenuAsync(juce::PopupMenu::Options(),
                    [this, ci](int result)
                    {
                        if (result == 0) return;
                        if (ci < 0 || ci >= placedClips.size()) return;

                        if (result == 101)
                        {
                            // Open Pattern Editor for this clip's pattern.
                            const int patternIdx = placedClips[ci].patternIndex;
                            if (patternIdx >= 0 && patternIdx < patternNames.size())
                                openPatternEditor(patternIdx);
                            return;
                        }

                        if (result == 102)
                        {
                            // Delete clip — use the same Action machinery as
                            // dragging-to-browser-deletes so undo restores it.
                            const int patternIdx = placedClips[ci].patternIndex;

                            // Block deletion if this clip uses the pattern
                            // currently being inspected (parity with drag-to-
                            // browser delete in mouseUp).
                            if (isInspecting() && patternIdx == inspectedPatternIndex)
                            {
                                DBG("Clip delete blocked: pattern is currently being inspected");
                                return;
                            }

                            Action action;
                            action.type = Action::RemoveClip;
                            action.clip = placedClips[ci];
                            action.clipIndex = ci;
                            undoStack.add(action);
                            redoStack.clear();
                            if (undoStack.size() > 10) undoStack.remove(0);

                            deleteClip(placedClips[ci].clipId);
                            placedClips.remove(ci);
                            repaint();
                            return;
                        }

                        // Move to Track t — IDs 200..200+N-1
                        if (result >= 200 && result < 200 + trackNames.size())
                        {
                            const int newTrack = result - 200;
                            if (newTrack == placedClips[ci].trackIndex) return;

                            Action action;
                            action.type = Action::MoveClip;
                            action.clipIndex = ci;
                            action.previousClip = placedClips[ci];
                            action.clip = placedClips[ci];
                            action.clip.trackIndex = newTrack;
                            undoStack.add(action);
                            redoStack.clear();
                            if (undoStack.size() > 10) undoStack.remove(0);

                            placedClips.getReference(ci).trackIndex = newTrack;
                            updateClip(placedClips[ci]);
                            repaint();
                            return;
                        }
                    });
                return;
            }
        }
    }
    // Check track remove button
    for (int i = 0; i < trackNames.size(); ++i)
    {
        int y = trackAreaTop + i * trackHeight - (int)trackScrollOffset;
        int patternWidth = 150;
        juce::Rectangle<int> removeBtn(patternWidth + 4, y + 10, 20, 20);
        if (removeBtn.contains(e.x, e.y))
        {
            removeTrack(i);
            return;
        }
    }
}
void DAWComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int patternAreaTop = gridTop + 28;

    for (int i = 0; i < patternNames.size(); ++i)
    {
        int y = patternAreaTop + i * patternHeight - (int)patternScrollOffset;
        juce::Rectangle<int> patternRect(4, y, 150 - 8, patternHeight - 4);
        if (patternRect.contains(e.x, e.y))
        {
            openPatternEditor(i);
            return;
        }
    }
}

void DAWComponent::addPattern()
{
    juce::String newName = "Pattern " + juce::String(patternNames.size() + 1);
    int newPatternId = -1;

    // Save to database
    if (currentProjectId >= 0)
    {
        std::string projectIdStr = std::to_string(currentProjectId);
        const char* params[2] = { projectIdStr.c_str(), newName.toRawUTF8() };
        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "INSERT INTO Patterns (project_id, name) VALUES ($1, $2) RETURNING pattern_id",
            2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) == PGRES_TUPLES_OK)
        {
            newPatternId = std::stoi(PQgetvalue(res, 0, 0));
            DBG("Pattern saved with ID: " + juce::String(newPatternId));
        }
        else
        {
            DBG("Pattern save error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        }
        PQclear(res);
    }

    patternNames.add(newName);
    patternIds.add(newPatternId);
    Action action;
    action.type = Action::AddPattern;
    action.patternName = newName;
    action.patternId = newPatternId;
    action.patternIndex = patternNames.size() - 1;
    undoStack.add(action);
    redoStack.clear();
    if (undoStack.size() > 10) undoStack.remove(0);

    int totalPatternHeight = patternNames.size() * patternHeight;
    patternScrollBar.setRangeLimits(0.0, totalPatternHeight);
    loadPatternNotes();
    loadFullPatternNotes();
    repaint();
    resized();

    // Auto-prompt for rename — same flow as right-click > Rename. Mirrors
    // the geometry math used there so the inline editor lands exactly on
    // the new pattern's row in the browser.
    const int newIndex = patternNames.size() - 1;
    constexpr int menuBarHeight = 25;
    constexpr int toolbarHeight = 40;
    constexpr int toolbar2Height = 35;
    const int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    const int patternAreaTop = gridTop + 28;
    constexpr int gridLeft = 200;

    // Make sure the new row is visible — scroll it into view if it's off-screen.
    const int rowYContent = newIndex * patternHeight;
    const int patternAreaHeight = getHeight() - gridTop - 60;
    if (rowYContent + patternHeight > patternScrollOffset + patternAreaHeight)
    {
        patternScrollOffset = std::max(0.0,
            (double)(rowYContent + patternHeight - patternAreaHeight));
        patternScrollBar.setCurrentRangeStart(patternScrollOffset);
    }

    const int y = patternAreaTop + rowYContent - (int)patternScrollOffset;

    // Suppress one focus-lost — opening this editor steals focus from
    // wherever it was, generating a stray focus-loss notification.
    suppressNextRenameFocusLost = true;
    juce::MessageManager::callAsync(
        [this]() { suppressNextRenameFocusLost = false; });

    editingPatternIndex = newIndex;
    patternRenameEditor.setText(newName);
    patternRenameEditor.setBounds(4, y, gridLeft - 8, patternHeight - 4);
    patternRenameEditor.setVisible(true);
    patternRenameEditor.grabKeyboardFocus();
    patternRenameEditor.selectAll();
}

void DAWComponent::openPatternEditor(int index)
{
    // Block opening the editor for the pattern currently being inspected —
    // we don't want the user modifying a pattern while the inspector is showing
    // its phoneme breakdown.
    if (isInspecting() && index == inspectedPatternIndex)
    {
        DBG("Pattern editor blocked: pattern is currently being inspected");
        return;
    }

    int patternId = (index < patternIds.size()) ? patternIds[index] : -1;
    auto* window = new PatternEditorWindow("Pattern Editor: " + patternNames[index]);
    auto* roll = new PianoRollComponent(patternId);
    window->setContentOwned(roll, true);
    window->centreWithSize(1280, 600);
    window->setVisible(true);

    // Reload note previews and full notes when the editor is closed
    roll->onEditorClosed = [this]()
        {
            loadPatternNotes();
            loadFullPatternNotes();
            repaint();
        };
}

void DAWComponent::drawPatternBrowser(juce::Graphics& g, int gridTop, int patternWidth)
{
    int patternAreaTop = gridTop + 28;

    for (int i = 0; i < patternNames.size(); ++i)
    {
        int y = patternAreaTop + i * patternHeight - (int)patternScrollOffset;

        if (y + patternHeight < patternAreaTop || y > getHeight() - 70) continue;

        g.setColour(juce::Colour(70, 20, 110));
        g.fillRoundedRectangle(4, y, patternWidth - 14, patternHeight - 4, 4.0f);

        // Draw note preview as horizontal bars (mini piano roll)
        if (i < patternNotePreviews.size())
        {
            auto& notes = patternNotePreviews.getReference(i);
            if (!notes.isEmpty())
            {
                int maxBeat = 32;
                int minPitch = 127, maxPitch = 0;
                for (auto& note : notes)
                {
                    minPitch = std::min(minPitch, note.pitch);
                    maxPitch = std::max(maxPitch, note.pitch);
                }
                int pitchRange = std::max(maxPitch - minPitch, 12);

                int previewW = patternWidth - 18;
                int innerTop = y + 4;
                int innerH = patternHeight - 8;
                int barH = std::max(2, innerH / pitchRange);
                int barW = std::max(3, previewW / maxBeat);

                for (auto& note : notes)
                {
                    float noteXRatio = (float)note.beat / maxBeat;
                    float noteYRatio = 1.0f - (float)(note.pitch - minPitch) / pitchRange;
                    int nx = 6 + (int)(noteXRatio * (previewW - barW));
                    int ny = innerTop + (int)(noteYRatio * (innerH - barH));
                    g.setColour(juce::Colours::white.withAlpha(0.55f));
                    g.fillRect(nx, ny, barW, barH);
                }
            }
        }

        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(patternNames[i], 10, y, patternWidth - 24, patternHeight - 4, juce::Justification::centredLeft);
    }
}

void DAWComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int patternWidth = 150;
    int trackHeaderWidth = 80;
    int gridLeft = patternWidth + trackHeaderWidth;

    if (e.x < patternWidth)
    {
        double newOffset = patternScrollOffset - (double)wheel.deltaY * 60.0;
        newOffset = std::max(0.0, std::min(newOffset, patternScrollBar.getRangeLimit().getEnd()));
        patternScrollBar.setCurrentRangeStart(newOffset);
    }
    else if (e.x < gridLeft)
    {
        double newOffset = trackScrollOffset - (double)wheel.deltaY * 60.0;
        newOffset = std::max(0.0, std::min(newOffset, trackScrollBar.getRangeLimit().getEnd()));
        trackScrollBar.setCurrentRangeStart(newOffset);
    }
    else
    {
        double newOffset = trackScrollOffset - (double)wheel.deltaY * 60.0;
        newOffset = std::max(0.0, std::min(newOffset, trackScrollBar.getRangeLimit().getEnd()));
        trackScrollBar.setCurrentRangeStart(newOffset);

        double newHOffset = horizontalScrollOffset - (double)wheel.deltaX * 60.0;
        newHOffset = std::max(0.0, std::min(newHOffset, horizontalScrollBar.getRangeLimit().getEnd()));
        horizontalScrollBar.setCurrentRangeStart(newHOffset);
    }
}

void DAWComponent::timerCallback()
{
    if (isPlaying)
    {
        double beatsPerFrame = (currentBPM / 60.0) / 60.0;
        playheadPosition += beatsPerFrame;

        // Beat-crossing detection for vocal synthesis
        int currentBeat = (int)playheadPosition;
        if (currentBeat != lastTriggeredBeat)
        {
            lastTriggeredBeat = currentBeat;
            triggerNotesAtBeat(currentBeat);
        }

        // Metronome visual flash (audio is handled by VocalSynthEngine audio thread)
        if (metronomeEnabled && vocalSynth.didMetronomeTick())
        {
            metronomeBeat = true;
            vocalSynth.clearMetronomeTick();

            // Educational mode: pulse the metronome button on every tick
            if (EducationalModeManager::getInstance().isEnabled())
                highlightOverlay.highlight(&metronomeButton, juce::Colours::cyan);
        }
        else
        {
            metronomeBeat = false;
        }

        repaint();
    }
}

void DAWComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDraggingPattern && !isDraggingClip)
    {
        int menuBarHeight = 25;
        int toolbarHeight = 40;
        int toolbar2Height = 35;
        int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
        int patternAreaTop = gridTop + 28;
        int trackAreaTop = gridTop + 20;
        int patternWidth = 150;
        int trackHeaderWidth = 80;
        int gridLeft = patternWidth + trackHeaderWidth;
        int cellWidth = (int)(80.0f * cellWidthMultiplier);

        // Check if drag started on an existing clip
        for (int i = 0; i < placedClips.size(); ++i)
        {
            auto& clip = placedClips.getReference(i);
            int trackY = trackAreaTop + clip.trackIndex * trackHeight - (int)trackScrollOffset;
            int clipX = gridLeft + (int)(clip.startBeat * cellWidth) - (int)horizontalScrollOffset;
            int clipW = (int)(clip.duration * cellWidth);
            juce::Rectangle<int> clipRect(clipX, trackY, clipW, trackHeight);

            if (clipRect.contains(e.getMouseDownX(), e.getMouseDownY()))
            {
                isDraggingClip = true;
                draggingClipIndex = i;
                break;
            }
        }

        // Check if drag started on a pattern in the browser
        if (!isDraggingClip)
        {
            for (int i = 0; i < patternNames.size(); ++i)
            {
                int y = patternAreaTop + i * patternHeight - (int)patternScrollOffset;
                juce::Rectangle<int> patternRect(4, y, 150 - 8, patternHeight - 4);
                if (patternRect.contains(e.getMouseDownX(), e.getMouseDownY()))
                {
                    isDraggingPattern = true;
                    draggingPatternIndex = i;
                    break;
                }
            }
        }
    }

    if (isDraggingPattern || isDraggingClip)
    {
        dragX = e.x;
        dragY = e.y;
        repaint();
    }
}

void DAWComponent::mouseUp(const juce::MouseEvent& e)
{
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int trackAreaTop = gridTop + 20;
    int patternWidth = 150;
    int trackHeaderWidth = 80;
    int gridLeft = patternWidth + trackHeaderWidth;
    int cellWidth = (int)(80.0f * cellWidthMultiplier);

    if (isDraggingClip && draggingClipIndex >= 0)
    {
        if (e.x < patternWidth)
        {
            // Block deleting a clip whose pattern is currently being inspected
            if (isInspecting()
                && placedClips[draggingClipIndex].patternIndex == inspectedPatternIndex)
            {
                DBG("Clip delete blocked: pattern is currently being inspected");
                isDraggingClip = false;
                draggingClipIndex = -1;
                repaint();
                return;
            }

            Action action;
            action.type = Action::RemoveClip;
            action.clip = placedClips[draggingClipIndex];
            action.clipIndex = draggingClipIndex;
            undoStack.add(action);
            redoStack.clear();
            if (undoStack.size() > 10) undoStack.remove(0);

            deleteClip(placedClips[draggingClipIndex].clipId);
            placedClips.remove(draggingClipIndex);
        }
        else
        {
            for (int i = 0; i < trackNames.size(); ++i)
            {
                int y = trackAreaTop + i * trackHeight - (int)trackScrollOffset;
                juce::Rectangle<int> trackRect(gridLeft, y, getWidth() - gridLeft, trackHeight);

                if (trackRect.contains(e.x, e.y))
                {
                    double beat = (double)(e.x - gridLeft + (int)horizontalScrollOffset) / cellWidth;
                    beat = std::max(0.0, beat);

                    if (!e.mods.isShiftDown())
                    {
                        double snapThreshold = 0.5;

                        // Snap to beat grid
                        double nearestBeat = std::round(beat);
                        if (std::abs(beat - nearestBeat) < snapThreshold)
                            beat = nearestBeat;

                        // Snap to other clip edges
                        for (int j = 0; j < placedClips.size(); ++j)
                        {
                            if (j == draggingClipIndex) continue;
                            auto& other = placedClips.getReference(j);
                            if (other.trackIndex == i)
                            {
                                if (std::abs(beat - (other.startBeat + other.duration)) < snapThreshold)
                                    beat = other.startBeat + other.duration;
                                else if (std::abs(beat - other.startBeat) < snapThreshold)
                                    beat = other.startBeat - placedClips[draggingClipIndex].duration;
                            }
                        }
                    }

                    Action action;
                    action.type = Action::MoveClip;
                    action.clipIndex = draggingClipIndex;
                    action.previousClip = placedClips[draggingClipIndex];
                    action.clip = placedClips[draggingClipIndex];
                    action.clip.startBeat = beat;
                    action.clip.trackIndex = i;
                    undoStack.add(action);
                    redoStack.clear();
                    if (undoStack.size() > 10) undoStack.remove(0);

                    placedClips.getReference(draggingClipIndex).startBeat = beat;
                    placedClips.getReference(draggingClipIndex).trackIndex = i;
                    updateClip(placedClips[draggingClipIndex]);
                    break;
                }
            }
        }
    }
    else if (isDraggingPattern && draggingPatternIndex >= 0)
    {
        // Auto-sized duration based on the pattern's note content
        double newClipDuration = (draggingPatternIndex < patternDurations.size())
            ? patternDurations[draggingPatternIndex]
            : 4.0;

        for (int i = 0; i < trackNames.size(); ++i)
        {
            int y = trackAreaTop + i * trackHeight - (int)trackScrollOffset;
            juce::Rectangle<int> trackRect(gridLeft, y, getWidth() - gridLeft, trackHeight);

            if (trackRect.contains(e.x, e.y))
            {
                double beat = (double)(e.x - gridLeft + (int)horizontalScrollOffset) / cellWidth;
                beat = std::max(0.0, beat);

                if (!e.mods.isShiftDown())
                {
                    double snapThreshold = 0.5;

                    // Snap to beat grid
                    double nearestBeat = std::round(beat);
                    if (std::abs(beat - nearestBeat) < snapThreshold)
                        beat = nearestBeat;

                    // Snap to other clip edges
                    for (auto& clip : placedClips)
                    {
                        if (clip.trackIndex == i)
                        {
                            if (std::abs(beat - (clip.startBeat + clip.duration)) < snapThreshold)
                                beat = clip.startBeat + clip.duration;
                            else if (std::abs(beat - clip.startBeat) < snapThreshold)
                                beat = clip.startBeat - newClipDuration;
                        }
                    }
                }

                PlacedClip clip;
                clip.patternIndex = draggingPatternIndex;
                clip.trackIndex = i;
                clip.startBeat = beat;
                clip.duration = newClipDuration;
                saveClip(clip);                 // populates clip.clipId
                placedClips.add(clip);          // now stored with valid DB id

                Action action;
                action.type = Action::AddClip;
                action.clip = clip;             // includes clipId for undo/redo
                action.clipIndex = placedClips.size() - 1;
                undoStack.add(action);
                redoStack.clear();
                if (undoStack.size() > 10) undoStack.remove(0);
                break;
            }
        }
    }

    isDraggingPattern = false;
    isDraggingClip = false;
    draggingPatternIndex = -1;
    draggingClipIndex = -1;
    repaint();
}

void DAWComponent::loadPatterns()
{
    if (currentProjectId < 0) return;

    patternNames.clear();
    patternIds.clear();

    try
    {
        std::string projectIdStr = std::to_string(currentProjectId);
        const char* params[1] = { projectIdStr.c_str() };
        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "SELECT pattern_id, name FROM Patterns WHERE project_id = $1 ORDER BY pattern_id ASC",
            1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) == PGRES_TUPLES_OK)
        {
            int rows = PQntuples(res);
            for (int row = 0; row < rows; ++row)
            {
                int id = std::stoi(PQgetvalue(res, row, 0));
                juce::String name = juce::String(PQgetvalue(res, row, 1));
                patternIds.add(id);
                patternNames.add(name);
            }
            DBG("Loaded " + juce::String(patternNames.size()) + " patterns");
        }
        else
        {
            DBG("Pattern load error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        }
        PQclear(res);
    }
    catch (const std::exception& e)
    {
        DBG("Pattern load error: " + juce::String(e.what()));
    }

    int totalPatternHeight = patternNames.size() * patternHeight;
    patternScrollBar.setRangeLimits(0.0, totalPatternHeight);
    repaint();
    resized();
}

void DAWComponent::performUndo()
{
    if (undoStack.isEmpty()) return;

    Action action = undoStack.getLast();
    undoStack.removeLast();

    bool needsPatternReload = false;

    switch (action.type)
    {
    case Action::AddPattern:
    {
        // Delete the row we added. Stored pattern_id goes stale; redo will
        // insert a fresh row and capture the new id.
        if (action.patternId >= 0)
        {
            std::string idStr = std::to_string(action.patternId);
            const char* params[1] = { idStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "DELETE FROM Patterns WHERE pattern_id = $1",
                1, nullptr, params, nullptr, nullptr, 0);
            PQclear(res);
        }

        // Shift later clips' patternIndex down (should be none for a freshly-
        // added pattern, but defensive against weird sequences).
        for (int c = placedClips.size() - 1; c >= 0; --c)
        {
            if (placedClips[c].patternIndex == action.patternIndex)
                placedClips.remove(c);
            else if (placedClips[c].patternIndex > action.patternIndex)
                placedClips.getReference(c).patternIndex--;
        }

        patternNames.remove(action.patternIndex);
        patternIds.remove(action.patternIndex);
        needsPatternReload = true;
        break;
    }

    case Action::RemovePattern:
    {
        // Re-insert pattern row (new id), its notes, and orphaned clips.
        int restoredPatternId = -1;
        if (currentProjectId >= 0)
        {
            std::string projectIdStr = std::to_string(currentProjectId);
            const char* params[2] = { projectIdStr.c_str(), action.patternName.toRawUTF8() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "INSERT INTO Patterns (project_id, name) VALUES ($1, $2) RETURNING pattern_id",
                2, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) == PGRES_TUPLES_OK)
                restoredPatternId = std::stoi(PQgetvalue(res, 0, 0));
            else
                DBG("Pattern restore error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
            PQclear(res);
        }

        // Shift later clip patternIndices UP to make room
        for (auto& c : placedClips)
            if (c.patternIndex >= action.patternIndex)
                c.patternIndex++;

        patternNames.insert(action.patternIndex, action.patternName);
        patternIds.insert(action.patternIndex, restoredPatternId);

        // Re-insert PatternNotes
        if (restoredPatternId >= 0)
        {
            std::string pidStr = std::to_string(restoredPatternId);
            for (const auto& n : action.savedNotes)
            {
                std::string pitchStr = std::to_string(n.pitch);
                std::string beatStr = std::to_string(n.beat);
                std::string durStr = std::to_string(n.duration);
                const char* np[5] = { pidStr.c_str(), pitchStr.c_str(), beatStr.c_str(),
                                      n.lyric.toRawUTF8(), durStr.c_str() };
                PGresult* nr = PQexecParams(DatabaseManager::get().db(),
                    "INSERT INTO PatternNotes (pattern_id, pitch, beat, lyric, duration) "
                    "VALUES ($1, $2, $3, $4, $5)",
                    5, nullptr, np, nullptr, nullptr, 0);
                if (PQresultStatus(nr) != PGRES_COMMAND_OK)
                    DBG("Note restore error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                PQclear(nr);
            }
        }

        // Re-insert orphaned clips (memory + DB). They carry their old
        // patternIndex which is exactly where we just inserted the pattern.
        const int firstNewClipIndex = placedClips.size();
        for (auto clip : action.orphanedClips)   // copy so saveClip can mutate id
        {
            saveClip(clip);
            placedClips.add(clip);
        }

        // Patch the Action's stored patternId + orphaned clip ids so the next
        // redo targets the new DB rows (originals are gone).
        action.patternId = restoredPatternId;
        for (int k = 0; k < action.orphanedClips.size(); ++k)
        {
            const int memIndex = firstNewClipIndex + k;
            if (memIndex < placedClips.size())
                action.orphanedClips.getReference(k).clipId = placedClips[memIndex].clipId;
        }

        needsPatternReload = true;
        break;
    }

    case Action::AddClip:
        // Delete from DB + memory
        if (action.clipIndex >= 0 && action.clipIndex < placedClips.size())
        {
            deleteClip(placedClips[action.clipIndex].clipId);
            placedClips.remove(action.clipIndex);
        }
        break;

    case Action::RemoveClip:
    {
        // Re-insert into DB (new id) + memory
        PlacedClip c = action.clip;
        saveClip(c);                           // populates new clipId
        placedClips.insert(action.clipIndex, c);
        action.clip = c;                        // patch so redo knows the new id
        break;
    }

    case Action::MoveClip:
        if (action.clipIndex >= 0 && action.clipIndex < placedClips.size())
        {
            // previousClip carries the old position; preserve the current
            // clipId (already correct) before overwriting.
            int keepId = placedClips[action.clipIndex].clipId;
            placedClips.getReference(action.clipIndex) = action.previousClip;
            placedClips.getReference(action.clipIndex).clipId = keepId;
            updateClip(placedClips[action.clipIndex]);
        }
        break;

    case Action::AddTrack:
        // Delete the DB row for this track, drop from memory.
        if (action.trackIndex >= 0 && action.trackIndex < trackIds.size())
        {
            deleteTrackFromDB(trackIds[action.trackIndex]);
            trackIds.remove(action.trackIndex);
        }
        if (action.trackIndex >= 0 && action.trackIndex < trackNames.size())
            trackNames.remove(action.trackIndex);
        {
            int totalHeight = trackNames.size() * trackHeight;
            trackScrollBar.setRangeLimits(0.0, totalHeight);
        }
        break;

    case Action::RemoveTrack:
    {
        // Re-insert into DB (new id) and back into memory at original index.
        int newTrackId = -1;
        if (currentProjectId >= 0)
        {
            std::string projectIdStr = std::to_string(currentProjectId);
            std::string orderIndexStr = std::to_string(action.trackIndex);
            const char* params[3] = { projectIdStr.c_str(),
                                      action.trackName.toRawUTF8(),
                                      orderIndexStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "INSERT INTO Tracks (project_id, name, type, order_index) VALUES ($1, $2, 'vocal', $3) RETURNING track_id",
                3, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) == PGRES_TUPLES_OK)
                newTrackId = std::stoi(PQgetvalue(res, 0, 0));
            else
                DBG("Track restore error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
            PQclear(res);
        }

        trackNames.insert(action.trackIndex, action.trackName);
        trackIds.insert(action.trackIndex, newTrackId);
        action.trackId = newTrackId;

        int totalHeight = trackNames.size() * trackHeight;
        trackScrollBar.setRangeLimits(0.0, totalHeight);
        break;
    }
    }

    redoStack.add(action);
    if (redoStack.size() > 10) redoStack.remove(0);

    if (needsPatternReload)
    {
        loadPatternNotes();
        loadFullPatternNotes();
    }
    repaint();
    resized();
}

void DAWComponent::performRedo()
{
    if (redoStack.isEmpty()) return;

    Action action = redoStack.getLast();
    redoStack.removeLast();

    bool needsPatternReload = false;

    switch (action.type)
    {
    case Action::AddPattern:
    {
        // Re-insert pattern row into DB (new id), memory, parallel arrays.
        int newPatternId = -1;
        if (currentProjectId >= 0)
        {
            std::string projectIdStr = std::to_string(currentProjectId);
            const char* params[2] = { projectIdStr.c_str(), action.patternName.toRawUTF8() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "INSERT INTO Patterns (project_id, name) VALUES ($1, $2) RETURNING pattern_id",
                2, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) == PGRES_TUPLES_OK)
                newPatternId = std::stoi(PQgetvalue(res, 0, 0));
            PQclear(res);
        }

        // Shift later clip patternIndices up
        for (auto& c : placedClips)
            if (c.patternIndex >= action.patternIndex)
                c.patternIndex++;

        patternNames.insert(action.patternIndex, action.patternName);
        patternIds.insert(action.patternIndex, newPatternId);
        action.patternId = newPatternId;
        needsPatternReload = true;
        break;
    }

    case Action::RemovePattern:
    {
        // Delete pattern row (DB cascade handles notes + clips). Drop orphaned
        // clips from memory, shift later clip patternIndices down.
        if (action.patternIndex >= 0 && action.patternIndex < patternIds.size()
            && patternIds[action.patternIndex] >= 0)
        {
            std::string idStr = std::to_string(patternIds[action.patternIndex]);
            const char* params[1] = { idStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "DELETE FROM Patterns WHERE pattern_id = $1",
                1, nullptr, params, nullptr, nullptr, 0);
            PQclear(res);
        }

        for (int c = placedClips.size() - 1; c >= 0; --c)
        {
            if (placedClips[c].patternIndex == action.patternIndex)
                placedClips.remove(c);
            else if (placedClips[c].patternIndex > action.patternIndex)
                placedClips.getReference(c).patternIndex--;
        }

        patternNames.remove(action.patternIndex);
        patternIds.remove(action.patternIndex);
        needsPatternReload = true;
        break;
    }

    case Action::AddClip:
    {
        // Re-insert the clip (new id) into DB + memory at original index.
        PlacedClip c = action.clip;
        saveClip(c);
        placedClips.insert(action.clipIndex, c);
        action.clip = c;
        break;
    }

    case Action::RemoveClip:
        if (action.clipIndex >= 0 && action.clipIndex < placedClips.size())
        {
            deleteClip(placedClips[action.clipIndex].clipId);
            placedClips.remove(action.clipIndex);
        }
        break;

    case Action::MoveClip:
        if (action.clipIndex >= 0 && action.clipIndex < placedClips.size())
        {
            int keepId = placedClips[action.clipIndex].clipId;
            placedClips.getReference(action.clipIndex) = action.clip;
            placedClips.getReference(action.clipIndex).clipId = keepId;
            updateClip(placedClips[action.clipIndex]);
        }
        break;

    case Action::AddTrack:
    {
        int newTrackId = -1;
        if (currentProjectId >= 0)
        {
            std::string projectIdStr = std::to_string(currentProjectId);
            std::string orderIndexStr = std::to_string(action.trackIndex);
            const char* params[3] = { projectIdStr.c_str(),
                                      action.trackName.toRawUTF8(),
                                      orderIndexStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "INSERT INTO Tracks (project_id, name, type, order_index) VALUES ($1, $2, 'vocal', $3) RETURNING track_id",
                3, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) == PGRES_TUPLES_OK)
                newTrackId = std::stoi(PQgetvalue(res, 0, 0));
            PQclear(res);
        }
        trackNames.insert(action.trackIndex, action.trackName);
        trackIds.insert(action.trackIndex, newTrackId);
        action.trackId = newTrackId;

        int totalHeight = trackNames.size() * trackHeight;
        trackScrollBar.setRangeLimits(0.0, totalHeight);
        break;
    }

    case Action::RemoveTrack:
        if (action.trackIndex >= 0 && action.trackIndex < trackIds.size())
        {
            deleteTrackFromDB(trackIds[action.trackIndex]);
            trackIds.remove(action.trackIndex);
        }
        if (action.trackIndex >= 0 && action.trackIndex < trackNames.size())
            trackNames.remove(action.trackIndex);
        {
            int totalHeight = trackNames.size() * trackHeight;
            trackScrollBar.setRangeLimits(0.0, totalHeight);
        }
        break;
    }

    undoStack.add(action);
    if (undoStack.size() > 10) undoStack.remove(0);

    if (needsPatternReload)
    {
        loadPatternNotes();
        loadFullPatternNotes();
    }
    repaint();
    resized();
}

void DAWComponent::parseTimeSignature(const juce::String& timeSig, int& num, int& den) const
{
    juce::StringArray parts;
    parts.addTokens(timeSig, "/", "");
    num = (parts.size() > 0) ? parts[0].getIntValue() : 4;
    den = (parts.size() > 1) ? parts[1].getIntValue() : 4;
    if (num < 1) num = 4;
    if (den < 1) den = 4;
}

void DAWComponent::loadPatternNotes()
{
    patternNotePreviews.clear();

    for (int i = 0; i < patternIds.size(); ++i)
    {
        juce::Array<NotePreview> notes;
        int patternId = patternIds[i];

        if (patternId >= 0)
        {
            std::string patternIdStr = std::to_string(patternId);
            const char* params[1] = { patternIdStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "SELECT pitch, beat, duration FROM PatternNotes WHERE pattern_id = $1 ORDER BY beat ASC",
                1, nullptr, params, nullptr, nullptr, 0);

            if (PQresultStatus(res) == PGRES_TUPLES_OK)
            {
                int rows = PQntuples(res);
                for (int row = 0; row < rows; ++row)
                {
                    NotePreview n;
                    n.pitch = std::stoi(PQgetvalue(res, row, 0));
                    n.beat = std::stoi(PQgetvalue(res, row, 1));
                    n.duration = std::stoi(PQgetvalue(res, row, 2));
                    notes.add(n);
                }
            }
            else
            {
                DBG("Load pattern notes error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
            }
            PQclear(res);
        }

        patternNotePreviews.add(notes);
    }

    // Rebuild pattern durations from note content (auto-sizing)
    patternDurations.clear();
    for (int i = 0; i < patternNotePreviews.size(); ++i)
        patternDurations.add(getPatternDuration(i));

    // Sync any already-placed clips with the new pattern durations
    updateClipDurations();
}

double DAWComponent::getPatternDuration(int patternIndex) const
{
    double maxBeat = 0.0;

    if (patternIndex >= 0 && patternIndex < patternNotePreviews.size())
    {
        for (const auto& note : patternNotePreviews.getReference(patternIndex))
        {
            // A note occupies [beat, beat + duration). Extend pattern to cover it.
            double noteEnd = (double)(note.beat + std::max(1, note.duration));
            if (noteEnd > maxBeat) maxBeat = noteEnd;
        }
    }

    // Round up to nearest 4-beat measure, minimum 4 beats
    double duration = std::max(4.0, std::ceil(maxBeat / 4.0) * 4.0);
    return duration;
}

void DAWComponent::updateClipDurations()
{
    bool anyChanged = false;

    for (auto& clip : placedClips)
    {
        if (clip.patternIndex < 0 || clip.patternIndex >= patternDurations.size())
            continue;

        double target = patternDurations[clip.patternIndex];
        if (std::abs(clip.duration - target) > 0.01)
        {
            clip.duration = target;
            if (clip.clipId >= 0)
                updateClip(clip);   // persist the new duration
            anyChanged = true;
        }
    }

    if (anyChanged)
        repaint();
}

void DAWComponent::loadFullPatternNotes()
{
    patternFullNotes.clear();

    for (int i = 0; i < patternIds.size(); ++i)
    {
        juce::Array<FullNote> notes;
        int patternId = patternIds[i];

        if (patternId >= 0)
        {
            std::string patternIdStr = std::to_string(patternId);
            const char* params[1] = { patternIdStr.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "SELECT pitch, beat, lyric, duration FROM PatternNotes WHERE pattern_id = $1 ORDER BY beat ASC",
                1, nullptr, params, nullptr, nullptr, 0);

            if (PQresultStatus(res) == PGRES_TUPLES_OK)
            {
                int rows = PQntuples(res);
                for (int row = 0; row < rows; ++row)
                {
                    FullNote n;
                    n.pitch = std::stoi(PQgetvalue(res, row, 0));
                    n.beat = std::stoi(PQgetvalue(res, row, 1));
                    n.lyric = juce::String(PQgetvalue(res, row, 2));
                    n.duration = std::max(1, std::stoi(PQgetvalue(res, row, 3)));
                    notes.add(n);
                }
            }
            else
            {
                DBG("loadFullPatternNotes error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
            }
            PQclear(res);
        }

        patternFullNotes.add(notes);
    }
}

void DAWComponent::triggerNotesAtBeat(int globalBeat)
{
    for (const auto& clip : placedClips)
    {
        int localBeat = globalBeat - (int)clip.startBeat;

        if (localBeat < 0 || localBeat >= (int)clip.duration)
            continue;

        int pIdx = clip.patternIndex;
        if (pIdx >= patternFullNotes.size()) continue;

        for (const auto& note : patternFullNotes.getReference(pIdx))
        {
            if (note.beat == localBeat && note.lyric.isNotEmpty())
                vocalSynth.queueLyric(note.lyric, note.pitch,
                    (double)currentBPM,
                    (double)note.duration);
        }
    }
}

void DAWComponent::saveClip(PlacedClip& clip)
{
    if (currentProjectId < 0 || clip.patternIndex >= patternIds.size()) return;

    int patternId = patternIds[clip.patternIndex];
    std::string projectIdStr = std::to_string(currentProjectId);
    std::string patternIdStr = std::to_string(patternId);
    std::string trackIndexStr = std::to_string(clip.trackIndex);
    std::string startBeatStr = std::to_string(clip.startBeat);
    std::string durationStr = std::to_string(clip.duration);

    const char* params[5] = {
        projectIdStr.c_str(),
        patternIdStr.c_str(),
        trackIndexStr.c_str(),
        startBeatStr.c_str(),
        durationStr.c_str()
    };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "INSERT INTO PlacedClips (project_id, pattern_id, track_index, start_beat, duration) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING clip_id",
        5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        clip.clipId = std::stoi(PQgetvalue(res, 0, 0));
        DBG("Clip saved with ID: " + juce::String(clip.clipId));
    }
    else
    {
        DBG("Clip save error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    }

    PQclear(res);
}

void DAWComponent::deleteClip(int clipId)
{
    if (clipId < 0) return;

    std::string clipIdStr = std::to_string(clipId);
    const char* params[1] = { clipIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "DELETE FROM PlacedClips WHERE clip_id = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        DBG("Clip delete error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));

    PQclear(res);
}

void DAWComponent::loadClips()
{
    placedClips.clear();

    if (currentProjectId < 0) return;

    std::string projectIdStr = std::to_string(currentProjectId);
    const char* params[1] = { projectIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "SELECT clip_id, pattern_id, track_index, start_beat, duration FROM PlacedClips WHERE project_id = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        int rows = PQntuples(res);
        for (int row = 0; row < rows; ++row)
        {
            int clipId = std::stoi(PQgetvalue(res, row, 0));
            int patternId = std::stoi(PQgetvalue(res, row, 1));
            int trackIndex = std::stoi(PQgetvalue(res, row, 2));
            double startBeat = std::stod(PQgetvalue(res, row, 3));
            double duration = std::stod(PQgetvalue(res, row, 4));

            // Find the pattern index from the pattern ID
            int patternIndex = -1;
            for (int i = 0; i < patternIds.size(); ++i)
            {
                if (patternIds[i] == patternId)
                {
                    patternIndex = i;
                    break;
                }
            }

            if (patternIndex >= 0)
            {
                PlacedClip clip;
                clip.clipId = clipId;
                clip.patternIndex = patternIndex;
                clip.trackIndex = trackIndex;
                clip.startBeat = startBeat;
                clip.duration = duration;
                placedClips.add(clip);
            }
        }
        DBG("Loaded " + juce::String(placedClips.size()) + " clips");
    }
    else
    {
        DBG("Clip load error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    }
    PQclear(res);
}

void DAWComponent::updateClip(const PlacedClip& clip)
{
    if (clip.clipId < 0) return;

    std::string clipIdStr = std::to_string(clip.clipId);
    std::string trackIndexStr = std::to_string(clip.trackIndex);
    std::string startBeatStr = std::to_string(clip.startBeat);
    std::string durationStr = std::to_string(clip.duration);

    const char* params[4] = {
        trackIndexStr.c_str(),
        startBeatStr.c_str(),
        durationStr.c_str(),
        clipIdStr.c_str()
    };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "UPDATE PlacedClips SET track_index = $1, start_beat = $2, duration = $3 WHERE clip_id = $4",
        4, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        DBG("Clip update error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));

    PQclear(res);
}

void DAWComponent::saveTrack(const juce::String& trackName, int orderIndex)
{
    if (currentProjectId < 0) return;

    std::string projectIdStr = std::to_string(currentProjectId);
    std::string orderIndexStr = std::to_string(orderIndex);

    const char* params[3] = { projectIdStr.c_str(), trackName.toRawUTF8(), orderIndexStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "INSERT INTO Tracks (project_id, name, type, order_index) VALUES ($1, $2, 'vocal', $3) RETURNING track_id",
        3, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        int trackId = std::stoi(PQgetvalue(res, 0, 0));
        trackIds.add(trackId);
        DBG("Track saved with ID: " + juce::String(trackId));
    }
    else
    {
        DBG("Track save error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        trackIds.add(-1);
    }
    PQclear(res);
}

void DAWComponent::deleteTrackFromDB(int trackId)
{
    if (trackId < 0) return;

    std::string trackIdStr = std::to_string(trackId);
    const char* params[1] = { trackIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "DELETE FROM Tracks WHERE track_id = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        DBG("Track delete error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));

    PQclear(res);
}

void DAWComponent::loadTracks()
{
    trackNames.clear();
    trackIds.clear();

    if (currentProjectId < 0) return;

    std::string projectIdStr = std::to_string(currentProjectId);
    const char* params[1] = { projectIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "SELECT track_id, name FROM Tracks WHERE project_id = $1 ORDER BY order_index ASC",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        int rows = PQntuples(res);
        for (int row = 0; row < rows; ++row)
        {
            trackIds.add(std::stoi(PQgetvalue(res, row, 0)));
            trackNames.add(juce::String(PQgetvalue(res, row, 1)));
        }
        DBG("Loaded " + juce::String(trackNames.size()) + " tracks");
    }
    else
    {
        DBG("Track load error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    }
    PQclear(res);
}

// ============================================================================
//  Educational Mode Implementation
// ============================================================================

void DAWComponent::educationalModeChanged(bool isEnabled)
{
    // Toggle the inspector button visibility
    inspectorToggleButton.setVisible(isEnabled);

    // If ed-mode is being turned off, close any open inspector window
    if (!isEnabled)
        closeInspectorWindow();

    // Tooltips are now always on regardless of ed-mode (they're a baseline
    // help affordance for ALL users). updateTooltips ignores its argument.
    updateTooltips(isEnabled);

    resized();
    repaint();
}

void DAWComponent::updateTooltips(bool /*eduEnabled — ignored, tooltips are always on*/)
{
    auto t = [&](const juce::String& key) -> juce::String
        {
            return TooltipRegistry::get(key);
        };

    // Transport
    playButton.setTooltip(t("playButton"));
    pauseButton.setTooltip(t("pauseButton"));
    stopButton.setTooltip(t("stopButton"));
    metronomeButton.setTooltip(
        juce::String("METRONOME: Plays a steady click on every beat of your "
            "current time signature. Use it to keep time while recording "
            "or editing. The first beat of each measure is accented."));

    // Tempo + time signature
    tempoButton.setTooltip(t("bpmControl"));
    timeSigButton.setTooltip(t("timeSignature"));

    // Pattern + track management
    addPatternButton.setTooltip(t("addPattern"));
    addTrackButton.setTooltip(t("addTrack"));
    inspectorToggleButton.setTooltip(
        juce::String(juce::CharPointer_UTF8(
            "SYNTHESIS INSPECTOR: Opens a window that walks you through how "
            "the engine turns your lyrics into sung audio \xe2\x80\x94 one word at a time.")));

    // Mode buttons (no specific registry entries; generic fallback)
    selectModeButton.setTooltip(
        juce::String(juce::CharPointer_UTF8(
            "SELECT VOICE: Open the voice-bank selector to switch between Aaron, UTAU, and more.")));
    editModeButton.setTooltip(
        juce::String(juce::CharPointer_UTF8(
            "SETTINGS: Open the project settings panel to change the project name, master volume, BPM, and time signature.")));
}

void DAWComponent::showInspectorPatternPicker()
{
    if (patternNames.isEmpty())
    {
        DBG("Inspector: no patterns available to inspect");
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader("Inspect which pattern?");
    for (int i = 0; i < patternNames.size(); ++i)
        menu.addItem(i + 1, patternNames[i]);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(inspectorToggleButton),
        [this](int result)
        {
            if (result > 0)
                openInspectorForPattern(result - 1);
        });
}

void DAWComponent::openInspectorForPattern(int patternIndex)
{
    if (patternIndex < 0 || patternIndex >= patternNames.size())
        return;

    // Build unique-words-in-first-played-order from this pattern's notes
    juce::StringArray words;
    juce::StringArray pitches;
    buildInspectorWordList(patternIndex, words, pitches);

    // Push data into the inspector, then open the window
    synthInspector.setPatternData(patternNames[patternIndex], words, pitches);

    inspectedPatternIndex = patternIndex;

    if (inspectorWindow == nullptr)
    {
        inspectorWindow = new SynthesisInspectorWindow(&synthInspector);
        inspectorWindow->onClose = [this]()
            {
                closeInspectorWindow();
            };
    }
    else
    {
        // Already open — bring to front + update title
        inspectorWindow->setName("Synthesis Inspector: " + patternNames[patternIndex]);
        inspectorWindow->toFront(true);
    }

    inspectorWindow->setName("Synthesis Inspector: " + patternNames[patternIndex]);
    inspectorToggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 140, 200));
}

void DAWComponent::closeInspectorWindow()
{
    if (inspectorWindow != nullptr)
    {
        // DocumentWindow with setContentNonOwned is safe to delete;
        // it will NOT delete the contained SynthesisInspector (we still own it).
        inspectorWindow->setVisible(false);
        delete inspectorWindow;
        inspectorWindow = nullptr;
    }
    inspectedPatternIndex = -1;
    synthInspector.clearPatternData();
    inspectorToggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 80, 120));
}

void DAWComponent::buildInspectorWordList(int patternIndex,
    juce::StringArray& outWords,
    juce::StringArray& outPitches) const
{
    outWords.clear();
    outPitches.clear();

    if (patternIndex < 0 || patternIndex >= patternFullNotes.size())
        return;

    const auto& notes = patternFullNotes.getReference(patternIndex);

    // The notes come from the DB already sorted by beat ASC, so first-seen is
    // first-played. Multiple notes at the same beat: we take them in whatever
    // order the DB returned (lenient first-played order, as you put it).
    std::unordered_set<std::string> seen;

    for (const auto& note : notes)
    {
        if (note.lyric.isEmpty())
            continue;

        // Normalise for dedupe: lowercase, stripped. The display keeps original.
        juce::String lyric = note.lyric.trim();
        if (lyric.isEmpty())
            continue;

        // A lyric cell can contain multiple words (the engine splits on spaces).
        // For the word wheel we treat each space-separated word as its own entry.
        juce::StringArray tokens;
        tokens.addTokens(lyric, " ", "");

        for (const auto& token : tokens)
        {
            juce::String t = token.trim();
            if (t.isEmpty()) continue;

            std::string key = t.toLowerCase().toStdString();
            if (seen.insert(key).second)   // true = newly inserted
            {
                outWords.add(t);
                outPitches.add(gridPitchToNoteName(note.pitch));
            }
        }
    }
}

juce::String DAWComponent::gridPitchToNoteName(int gridPitch)
{
    // Grid pitch 0 = top row = highest note. The engine uses
    // midi = (95 - gridPitch) + 12  → grid 0 maps to MIDI 107 (B7),
    //                                 grid 35 maps to MIDI 72 (C5), etc.
    int midi = (95 - gridPitch) + 12;
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    int note = midi % 12;
    int octave = (midi / 12) - 1;   // MIDI 60 = C4
    return juce::String(names[note]) + juce::String(octave);
}

// ============================================================================
//  Async resource loading
// ============================================================================

void DAWComponent::ResourceLoader::run()
{
    // Stage 1: dictionary (fast, ~100ms, 126k entries).
    if (!threadShouldExit())
    {
        auto dictFile = resourcesDir.getChildFile("cmudict.txt");
        owner.vocalSynth.loadDictionary(dictFile);

        juce::MessageManager::callAsync([ownerPtr = &owner]()
            {
                ownerPtr->onDictionaryLoaded();
            });
    }

    // Stage 2: voice bank (slow — ~3400 WAV files, several seconds).
    if (!threadShouldExit())
    {
        // The Aaron bank is the initial default. UTAU (and any others) can be
        // hot-swapped in later via DAWComponent::openVoiceBankSelector().
        auto bankDir = resourcesDir.getChildFile("VoiceBank").getChildFile("Aaron");
        owner.vocalSynth.loadVoiceBank(bankDir);

        juce::MessageManager::callAsync([ownerPtr = &owner]()
            {
                ownerPtr->onVoiceBankLoaded();
            });
    }
}

void DAWComponent::onDictionaryLoaded()
{
    if (isDying.load()) return;
    // Dictionary is ready but voice bank still loading — update the status text
    loadingOverlay.setStatus("Loading voice bank samples...");
}

void DAWComponent::onVoiceBankLoaded()
{
    if (isDying.load()) return;
    isVocalBankReady.store(true);
    refreshTransportEnabled();
    loadingOverlay.setVisible(false);
    resized();
    repaint();
    DBG("Voice bank load complete - UI unblocked");
}

void DAWComponent::refreshTransportEnabled()
{
    const bool ready = isVocalBankReady.load();

    // Transport controls require audio resources
    playButton.setEnabled(ready);
    pauseButton.setEnabled(ready);
    stopButton.setEnabled(ready);
    metronomeButton.setEnabled(ready);

    // Inspector also requires the dictionary (which comes with the voice bank load)
    inspectorToggleButton.setEnabled(ready);

    // Dim the buttons visually when disabled
    const juce::Colour dimText = juce::Colour(120, 120, 140);
    const juce::Colour liveText = juce::Colours::white;
    const juce::Colour c = ready ? liveText : dimText;

    for (auto* b : { &playButton, &pauseButton, &stopButton, &metronomeButton, &inspectorToggleButton })
    {
        b->setColour(juce::TextButton::textColourOnId, c);
        b->setColour(juce::TextButton::textColourOffId, c);
    }
}

// ============================================================================
//  LoadingOverlay
// ============================================================================

DAWComponent::LoadingOverlay::LoadingOverlay()
{
    setInterceptsMouseClicks(true, false);   // swallows clicks so transport can't be hammered
    startTimerHz(30);   // animate dots
}

void DAWComponent::LoadingOverlay::setStatus(const juce::String& s)
{
    statusText = s;
    repaint();
}

void DAWComponent::LoadingOverlay::timerCallback()
{
    animPhase += 0.04f;
    if (animPhase > 1.0f) animPhase -= 1.0f;
    repaint();
}

void DAWComponent::LoadingOverlay::paint(juce::Graphics& g)
{
    // Dark semi-transparent wash over the whole DAW
    g.fillAll(juce::Colour(15, 15, 25).withAlpha(0.88f));

    auto r = getLocalBounds().toFloat();

    // Centered card
    const float cardW = 360.0f;
    const float cardH = 120.0f;
    juce::Rectangle<float> card(r.getCentreX() - cardW * 0.5f,
        r.getCentreY() - cardH * 0.5f,
        cardW, cardH);

    g.setColour(juce::Colour(30, 15, 50));
    g.fillRoundedRectangle(card, 10.0f);
    g.setColour(juce::Colours::hotpink.withAlpha(0.6f));
    g.drawRoundedRectangle(card, 10.0f, 1.5f);

    // Status text
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(15.0f, juce::Font::bold));
    g.drawText(statusText,
        card.withHeight(card.getHeight() - 40).translated(0, 8),
        juce::Justification::centred);

    // Animated dots: three dots pulsing in sequence
    const float dotY = card.getBottom() - 30.0f;
    const float centre = card.getCentreX();
    const float spacing = 18.0f;
    for (int i = 0; i < 3; ++i)
    {
        float phase = animPhase + i * 0.33f;
        if (phase > 1.0f) phase -= 1.0f;
        float alpha = 0.3f + 0.7f * std::abs(std::sin(phase * juce::MathConstants<float>::pi));
        g.setColour(juce::Colours::hotpink.withAlpha(alpha));
        g.fillEllipse(centre + (i - 1) * spacing - 4.0f, dotY, 8.0f, 8.0f);
    }
}
// ============================================================================
//  Help dialog — now an in-component overlay (no AlertWindow, instant show)
// ============================================================================

void DAWComponent::showHelpDialog()
{
    // Toggle the in-component overlay. Because helpOverlay is already a child
    // of this DAWComponent and fully constructed in our ctor, there's zero
    // system-window creation latency here - setVisible(true) just flips a
    // flag and triggers a repaint.
    helpOverlay.setMode(HelpOverlay::Mode::Help);
    helpOverlay.setBounds(getLocalBounds());
    helpOverlay.setVisible(true);
    helpOverlay.toFront(true);
}

void DAWComponent::showAboutDialog()
{
    // Same overlay, different content. setMode swaps title + body text.
    helpOverlay.setMode(HelpOverlay::Mode::About);
    helpOverlay.setBounds(getLocalBounds());
    helpOverlay.setVisible(true);
    helpOverlay.toFront(true);
}

// ============================================================================
//  HelpOverlay implementation
// ============================================================================

DAWComponent::HelpOverlay::HelpOverlay()
{
    // Intercept clicks on the backdrop so underlying DAW UI can't be touched
    // while help is open. allowClicksOnChildComponents = true so the Close
    // button and TextEditor still work.
    setInterceptsMouseClicks(true, true);

    titleLabel.setText("VocalNite Help", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    closeButton.setButtonText("X");
    closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 20, 80));
    closeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(80, 30, 120));
    closeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    closeButton.onClick = [this]() { setVisible(false); };
    addAndMakeVisible(closeButton);

    body.setReadOnly(true);
    body.setMultiLine(true, false);
    body.setScrollbarsShown(true);
    body.setCaretVisible(false);
    body.setPopupMenuEnabled(false);
    body.setColour(juce::TextEditor::backgroundColourId, juce::Colour(20, 15, 35));
    body.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    body.setColour(juce::TextEditor::outlineColourId, juce::Colours::hotpink.withAlpha(0.35f));
    body.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::hotpink.withAlpha(0.6f));
    body.setColour(juce::TextEditor::highlightColourId, juce::Colours::hotpink.withAlpha(0.3f));
    body.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    body.setText(getHelpBody(), juce::dontSendNotification);
    addAndMakeVisible(body);
}

juce::Rectangle<int> DAWComponent::HelpOverlay::getCardBounds() const
{
    // Card sits centered, capped so it doesn't get silly on large windows
    const int w = juce::jmin(720, getWidth() - 80);
    const int h = juce::jmin(560, getHeight() - 60);
    return juce::Rectangle<int>((getWidth() - w) / 2,
        (getHeight() - h) / 2,
        w, h);
}

void DAWComponent::HelpOverlay::paint(juce::Graphics& g)
{
    // Dim backdrop (click anywhere outside the card to dismiss)
    g.fillAll(juce::Colour(0, 0, 0).withAlpha(0.55f));

    auto card = getCardBounds().toFloat();

    juce::ColourGradient grad(juce::Colour(35, 25, 55),
        card.getCentreX(), card.getY(),
        juce::Colour(20, 15, 35),
        card.getCentreX(), card.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(card, 12.0f);

    g.setColour(juce::Colours::hotpink.withAlpha(0.6f));
    g.drawRoundedRectangle(card, 12.0f, 1.5f);

    // Header divider under the title
    const float divY = card.getY() + 52.0f;
    g.setColour(juce::Colours::hotpink.withAlpha(0.25f));
    g.drawLine(card.getX() + 14.0f, divY, card.getRight() - 14.0f, divY, 1.0f);
}

void DAWComponent::HelpOverlay::resized()
{
    auto card = getCardBounds();
    auto header = card.removeFromTop(52);
    closeButton.setBounds(header.removeFromRight(44).withSizeKeepingCentre(28, 28));
    titleLabel.setBounds(header.reduced(18, 0));
    body.setBounds(card.reduced(14, 10));
}

void DAWComponent::HelpOverlay::mouseDown(const juce::MouseEvent& e)
{
    // Click on the dimmed backdrop (outside the card) dismisses the overlay.
    if (!getCardBounds().contains(e.getPosition()))
        setVisible(false);
}

bool DAWComponent::HelpOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible(false);
        return true;
    }
    return false;
}

void DAWComponent::HelpOverlay::visibilityChanged()
{
    if (isVisible())
    {
        // Grab focus so Esc works even if the user hasn't clicked anything yet
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();
    }
}

juce::String DAWComponent::HelpOverlay::getHelpBody()
{
    return
        "VocalNite - Concatenative Vocal Synthesis DAW\n"
        "================================================\n\n"
        "Playback\n"
        "--------\n"
        "  Play      - start/resume playback (press again to pause)\n"
        "  Pause     - freeze playback; voices resume mid-sample on Play\n"
        "  Stop      - reset playhead to beat 0, wipe active voices\n"
        "  Metronome - toggle click on/off; accented downbeat each bar\n\n"
        "Patterns\n"
        "--------\n"
        "  New pattern       - use the '+' button in the pattern browser (left side)\n"
        "  Open piano roll   - double-click a pattern\n"
        "  Right-click       - rename, duplicate, or delete\n"
        "  Duplicate         - copies all notes into a new pattern\n"
        "  Delete            - removes pattern + every placed clip using it (undoable)\n\n"
        "Arrangement\n"
        "-----------\n"
        "  Drag pattern onto a track row to create a clip\n"
        "  Drag a clip horizontally to move it; clips snap to beat grid\n"
        "  Hold Shift while dragging for free (unsnapped) placement\n"
        "  Drag a clip back into the pattern browser to delete it\n"
        "  Clip duration auto-sizes from its pattern's note content\n\n"
        "Piano roll (inside a pattern)\n"
        "-----------------------------\n"
        "  Left-click empty cell  - place a note + open lyric editor\n"
        "  Left-click existing    - edit its lyric\n"
        "  Drag right edge        - stretch note duration (held longer when sung)\n"
        "  Right-click            - delete note\n"
        "  Enter                  - save lyric\n"
        "  Escape                 - discard lyric / cancel\n"
        "  Click outside editor   - cancels the edit (no new note created)\n"
        "  Click a different note - saves current, opens editor on the new one\n"
        "  Mouse wheel            - scroll vertically\n"
        "  Shift + wheel          - scroll horizontally\n\n"
        "Long notes (stretch feature)\n"
        "----------------------------\n"
        "  Drag a note's right edge to make it multi-beat. Consonants are sung\n"
        "  at natural speech rate; the final vowel is held for the extra time\n"
        "  with a gentle fade at the end, so 'hello' stretched to 4 beats sounds\n"
        "  like h-e-l-OOOOOooo rather than a slowed-down 'hello'.\n\n"
        "Edit / undo\n"
        "-----------\n"
        "  Ctrl+Z / Edit > Undo - up to 10 steps of undo for:\n"
        "    - adding or removing a pattern (restores notes on remove-undo)\n"
        "    - adding, removing, or moving a clip\n"
        "    - adding or removing a track\n"
        "  Ctrl+Y / Edit > Redo - replays the undone action\n\n"
        "Educational mode\n"
        "----------------\n"
        "  Only visible for verified .edu users (ProjectManager toggle).\n"
        "  When on:\n"
        "    - cyan highlights pulse on transport actions\n"
        "    - tooltips appear on every control (hover ~600ms)\n"
        "    - inside the piano roll, tooltips are pinned to the top-right\n"
        "    - Synthesis Inspector button opens a phoneme breakdown per word\n"
        "      across an entire pattern's lyric content\n\n"
        "Voice bank & phonemes\n"
        "---------------------\n"
        "  Uses the CMU Pronouncing Dictionary + ARPAsing-format WAV samples\n"
        "  organised by phoneme and pitch (A3, C4, F4 folders). The engine\n"
        "  prefers diphones (PREV-CUR) over solo phonemes for smoother\n"
        "  transitions, with equal-power crossfades between slots.\n\n"
        "Tips\n"
        "----\n"
        "  - If playback is silent, confirm voice bank loaded (transport\n"
        "    buttons un-dim once the overlay disappears)\n"
        "  - Unknown words (not in the dictionary) are skipped silently\n"
        "  - For best results, use simple English words in lyrics\n\n"
        "Press Esc or click outside this panel to close.\n";
}

juce::String DAWComponent::HelpOverlay::getAboutBody()
{
    return
        "VocalNite\n"
        "================================\n\n"
        "What is VocalNite?\n"
        "------------------\n"
        "  VocalNite is a concatenative vocal-synthesis DAW. You write notes\n"
        "  on a piano roll, attach lyrics to them, and the engine sings them\n"
        "  back to you using a phoneme-by-phoneme voice bank. Drop the same\n"
        "  pattern onto multiple tracks, sequence them on a timeline, and\n"
        "  build full vocal arrangements.\n\n"
        "Purpose\n"
        "-------\n"
        "  VocalNite was built to make vocal synthesis approachable. Most\n"
        "  vocal-synth tools either demand professional audio engineering\n"
        "  knowledge or hide the synthesis pipeline behind a black box.\n"
        "  VocalNite splits the difference: it works like a familiar DAW\n"
        "  (patterns, tracks, transport, BPM, time signatures) while letting\n"
        "  curious users peek at exactly how the engine stitches phonemes\n"
        "  into singing voice via the Synthesis Inspector (Educational Mode).\n\n"
        "How it works\n"
        "------------\n"
        "  Lyrics get split into words, words get looked up in the CMU\n"
        "  Pronouncing Dictionary to retrieve their ARPAbet phoneme sequence,\n"
        "  and each phoneme is rendered from a pre-recorded WAV sample at\n"
        "  the closest available pitch (A3 / C4 / F4). Diphones (PREV-CUR)\n"
        "  are preferred over solo phonemes for smoother transitions, and\n"
        "  the engine adds vibrato, slight timing jitter, and equal-power\n"
        "  crossfades to keep things sounding human.\n\n"
        "Educational Mode\n"
        "----------------\n"
        "  Verified .edu users get an extra layer: cyan-pulse highlights on\n"
        "  transport, the Synthesis Inspector window that shows phonemes per\n"
        "  word, and access to the UTAU voice bank in addition to Aaron.\n\n"
        "Voice banks\n"
        "-----------\n"
        "  Aaron is the canonical default voice. Educational users may also\n"
        "  swap to UTAU via the 'Select Voice' character-select screen on\n"
        "  the toolbar. Hot-swaps happen on a background thread so playback\n"
        "  doesn't interrupt anything else.\n\n"
        "Project storage\n"
        "---------------\n"
        "  Projects, patterns, notes, tracks, and clips are persisted to a\n"
        "  Supabase Postgres database, so your work survives between sessions\n"
        "  and across devices logged in to the same account.\n\n"
        "Press Esc or click outside this panel to close.\n";
}

void DAWComponent::HelpOverlay::setMode(Mode m)
{
    if (currentMode == m && body.getText().isNotEmpty())
        return;   // already showing this mode

    currentMode = m;
    if (m == Mode::Help)
    {
        titleLabel.setText("VocalNite Help", juce::dontSendNotification);
        body.setText(getHelpBody(), juce::dontSendNotification);
    }
    else
    {
        titleLabel.setText("About VocalNite", juce::dontSendNotification);
        body.setText(getAboutBody(), juce::dontSendNotification);
    }
    body.moveCaretToTop(false);   // scroll to start when switching modes
    repaint();
}

// ============================================================================
//  Voice-bank character select (fighting-game-style picker + hot swap)
// ============================================================================

juce::Array<VoiceBankSelectorOverlay::BankInfo>
DAWComponent::discoverAvailableBanks() const
{
    juce::Array<VoiceBankSelectorOverlay::BankInfo> banks;

    // Does this folder contain at least one pitch subfolder (A3, C4, F4, ...)?
    auto hasPitchFolders = [](const juce::File& dir) -> bool
        {
            if (!dir.isDirectory()) return false;
            for (juce::DirectoryEntry e : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findDirectories))
            {
                juce::ignoreUnused(e);
                return true;   // any subfolder counts; loadVoiceBank filters further
            }
            return false;
        };

    // Look for a portrait image inside the character's folder. Supported
    // filenames: portrait.png, portrait.jpg, portrait.jpeg (checked in order).
    // Returns an invalid juce::File if none exist.
    auto findPortrait = [](const juce::File& dir) -> juce::File
        {
            if (!dir.isDirectory()) return {};
            static const char* const names[] = { "portrait.png", "portrait.jpg", "portrait.jpeg" };
            for (const char* n : names)
            {
                juce::File f = dir.getChildFile(n);
                if (f.existsAsFile()) return f;
            }
            return {};
        };

    // Aaron — always listed when the folder exists (empty-content still counts
    // as "present" since it's our canonical default; UTAU is held to a stricter
    // bar because we hide it entirely when not populated).
    auto aaronFolder = voiceBankRoot.getChildFile("Aaron");
    if (aaronFolder.isDirectory())
    {
        VoiceBankSelectorOverlay::BankInfo aaron;
        aaron.id = "aaron";
        aaron.displayName = "Aaron";
        aaron.description = juce::String(juce::CharPointer_UTF8(
            "The Almighty Aaron..."));
        aaron.initial = "A";
        aaron.themeColour = juce::Colours::hotpink;
        aaron.bankFolder = aaronFolder;
        aaron.portraitFile = findPortrait(aaronFolder);
        banks.add(aaron);
    }

    // UTAU — educational-only feature. Hidden entirely for normal users
    // even if the folder exists. Edu users still need a populated folder
    // (at least one pitch subfolder) for the card to appear.
    if (currentUserType == "educational")
    {
        auto utauFolder = voiceBankRoot.getChildFile("UTAU");
        if (hasPitchFolders(utauFolder))
        {
            VoiceBankSelectorOverlay::BankInfo utau;
            utau.id = "utau";
            utau.displayName = "UTAU";
            utau.description = juce::String(juce::CharPointer_UTF8(
                "Ready to Speak!"));
            utau.initial = "U";
            utau.themeColour = juce::Colour(0, 220, 255);   // bright cyan
            utau.bankFolder = utauFolder;
            utau.portraitFile = findPortrait(utauFolder);
            banks.add(utau);
        }
    }

    return banks;
}

void DAWComponent::openVoiceBankSelector()
{
    // Rebuild the list each open — the user may have added a UTAU folder
    // between sessions without restarting the app.
    auto banks = discoverAvailableBanks();
    voiceBankSelectorOverlay.setAvailableBanks(banks, currentVoiceBankId);
    voiceBankSelectorOverlay.setBounds(getLocalBounds());
    voiceBankSelectorOverlay.setVisible(true);
    voiceBankSelectorOverlay.toFront(true);
}

void DAWComponent::onVoiceBankChosen(const juce::String& bankId)
{
    // No-op if user picked the currently-loaded bank (overlay should have
    // disabled that card's SELECT button already, but guard anyway).
    if (bankId.isEmpty() || bankId == currentVoiceBankId) return;

    // Resolve folder from the bank id
    juce::File bankFolder;
    auto banks = discoverAvailableBanks();
    for (const auto& b : banks)
        if (b.id == bankId) { bankFolder = b.bankFolder; break; }

    if (!bankFolder.isDirectory())
    {
        DBG("Voice bank folder not found for id: " + bankId);
        return;
    }

    // Full transport stop so no queueLyric calls race the swap. We do NOT
    // auto-resume after the swap — the user will press Play again.
    if (isPlaying)
    {
        isPlaying = false;
        stopTimer();
    }
    playheadPosition = 0.0;
    lastTriggeredBeat = -1;
    vocalSynth.setPaused(false);     // unpause so stop() can wipe voices cleanly
    vocalSynth.stop();
    vocalSynth.setMetronomeEnabled(false);

    // Gate the UI: transport dims until the new bank is ready.
    isVocalBankReady.store(false);
    refreshTransportEnabled();

    // Surface progress via the existing loading overlay.
    juce::String pretty = bankId.substring(0, 1).toUpperCase() + bankId.substring(1);
    loadingOverlay.setStatus("Switching to " + pretty + "...");
    loadingOverlay.setVisible(true);
    loadingOverlay.toFront(false);
    repaint();

    // Stop any in-flight swap thread defensively (shouldn't normally happen).
    if (voiceBankSwapThread != nullptr)
    {
        voiceBankSwapThread->stopThread(3000);
        voiceBankSwapThread.reset();
    }

    voiceBankSwapThread.reset(new VoiceBankSwapThread(*this, bankFolder, bankId));
    voiceBankSwapThread->startThread();
}

void DAWComponent::onVoiceBankSwapFinished(const juce::String& bankId, bool success)
{
    if (isDying.load()) return;

    if (success)
    {
        currentVoiceBankId = bankId;
        DBG("Voice bank swap complete: " + bankId);
        saveProjectSettings();
    }
    else
    {
        DBG("Voice bank swap FAILED for id: " + bankId);
    }

    isVocalBankReady.store(true);
    refreshTransportEnabled();
    loadingOverlay.setVisible(false);
    repaint();
}

void DAWComponent::VoiceBankSwapThread::run()
{
    if (threadShouldExit()) return;

    // reloadVoiceBank pauses the audio thread, wipes active + pending voices
    // under queueLock, repopulates the voiceBank map, then unpauses.
    const bool ok = owner.vocalSynth.reloadVoiceBank(bankFolder);

    juce::MessageManager::callAsync(
        [ownerPtr = &owner, id = bankId, ok]()
        {
            ownerPtr->onVoiceBankSwapFinished(id, ok);
        });
}
// ============================================================================
//  Project Settings — DB helpers
// ============================================================================

void DAWComponent::loadProjectSettings()
{
    if (currentProjectId < 0) return;

    try
    {
        std::string pidStr = std::to_string(currentProjectId);
        const char* params[1] = { pidStr.c_str() };
        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "SELECT bpm, time_signature, master_volume, voice_bank_index FROM Projects WHERE project_id = $1",
            1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
        {
            const char* bpmStr = PQgetvalue(res, 0, 0);
            const char* tsStr = PQgetvalue(res, 0, 1);

            if (bpmStr != nullptr && bpmStr[0] != '\0')
            {
                int bpm = std::atoi(bpmStr);
                if (bpm >= 30 && bpm <= 522)
                {
                    currentBPM = bpm;
                    vocalSynth.setTempo((double)currentBPM);
                    tempoButton.setButtonText("BPM: " + juce::String(currentBPM));
                }
            }

            if (tsStr != nullptr && tsStr[0] != '\0')
            {
                juce::String sig(tsStr);
                sig = sig.trim();
                if (sig.isNotEmpty())
                {
                    currentTimeSig = sig;
                    timeSigButton.setButtonText(currentTimeSig);
                    int num = 4, den = 4;
                    parseTimeSignature(currentTimeSig, num, den);
                    vocalSynth.setTimeSignature(num, den);
                }
            }
            const char* volStr = PQgetvalue(res, 0, 2);
            if (volStr != nullptr && volStr[0] != '\0')
            {
                float vol = (float)std::atof(volStr);
                masterVolume01 = juce::jlimit(0.0f, 1.5f, vol);
                vocalSynth.setMasterGain(masterVolume01);
            }

            const char* vbStr = PQgetvalue(res, 0, 3);
            if (vbStr != nullptr && vbStr[0] != '\0')
            {
                int vbIndex = std::atoi(vbStr);
                currentVoiceBankId = (vbIndex == 1) ? "utau" : "aaron";
            }
        }
        PQclear(res);
    }
    catch (const std::exception& e)
    {
        DBG("loadProjectSettings error: " + juce::String(e.what()));
    }
}

void DAWComponent::saveProjectSettings()
{
    if (currentProjectId < 0) return;

    try
    {
        std::string bpmStr = std::to_string(currentBPM);
        std::string pidStr = std::to_string(currentProjectId);
        juce::String ts = currentTimeSig;
        std::string volStr = std::to_string(masterVolume01);
        int vbIndex = (currentVoiceBankId == "utau") ? 1 : 0;
        std::string vbStr = std::to_string(vbIndex);

        const char* params[5] = { bpmStr.c_str(), ts.toRawUTF8(), volStr.c_str(), vbStr.c_str(), pidStr.c_str() };
        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "UPDATE Projects SET bpm = $1::int, time_signature = $2, master_volume = $3::float, voice_bank_index = $4::int WHERE project_id = $5",
            5, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            DBG("saveProjectSettings error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        PQclear(res);
    }
    catch (const std::exception& e)
    {
        DBG("saveProjectSettings error: " + juce::String(e.what()));
    }
}

void DAWComponent::saveProjectName(const juce::String& newName)
{
    if (currentProjectId < 0) return;

    try
    {
        std::string pidStr = std::to_string(currentProjectId);
        const char* params[2] = { newName.toRawUTF8(), pidStr.c_str() };
        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "UPDATE Projects SET name = $1 WHERE project_id = $2",
            2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            DBG("saveProjectName error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        PQclear(res);
    }
    catch (const std::exception& e)
    {
        DBG("saveProjectName error: " + juce::String(e.what()));
    }
}

void DAWComponent::openProjectSettings()
{
    // Seed the overlay with live values, then show it.
    projectSettingsOverlay.prime(currentProjectName,
        currentBPM,
        currentTimeSig,
        masterVolume01);
    projectSettingsOverlay.setBounds(getLocalBounds());
    projectSettingsOverlay.setVisible(true);
    projectSettingsOverlay.toFront(true);
}

// ============================================================================
//  ProjectSettingsOverlay implementation
// ============================================================================

DAWComponent::ProjectSettingsOverlay::ProjectSettingsOverlay()
{
    setInterceptsMouseClicks(true, true);

    titleLabel.setText("Project Settings", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    closeButton.setButtonText("X");
    closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 20, 80));
    closeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(80, 30, 120));
    closeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    closeButton.onClick = [this]() { setVisible(false); };
    addAndMakeVisible(closeButton);

    auto styleLabel = [](juce::Label& L, float fontSize, bool bold,
        juce::Colour col, juce::Justification just)
        {
            L.setFont(juce::Font(fontSize, bold ? juce::Font::bold : juce::Font::plain));
            L.setColour(juce::Label::textColourId, col);
            L.setJustificationType(just);
        };

    // Project Name
    styleLabel(nameLabel, 13.0f, true, juce::Colour(200, 160, 230),
        juce::Justification::centredLeft);
    nameLabel.setText("Project Name", juce::dontSendNotification);
    addAndMakeVisible(nameLabel);

    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(28, 20, 50));
    nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    nameEditor.setColour(juce::TextEditor::outlineColourId,
        juce::Colours::hotpink.withAlpha(0.4f));
    nameEditor.setColour(juce::TextEditor::focusedOutlineColourId,
        juce::Colours::hotpink.withAlpha(0.8f));
    nameEditor.setFont(juce::Font(15.0f));
    nameEditor.setSelectAllWhenFocused(true);
    nameEditor.onReturnKey = [this]()
        {
            if (onProjectNameCommitted)
                onProjectNameCommitted(nameEditor.getText());
        };
    nameEditor.onFocusLost = [this]()
        {
            if (onProjectNameCommitted)
                onProjectNameCommitted(nameEditor.getText());
        };
    addAndMakeVisible(nameEditor);

    // Master Volume
    styleLabel(volumeLabel, 13.0f, true, juce::Colour(200, 160, 230),
        juce::Justification::centredLeft);
    volumeLabel.setText("Master Volume", juce::dontSendNotification);
    addAndMakeVisible(volumeLabel);

    styleLabel(volumeValueLabel, 13.0f, false, juce::Colours::white,
        juce::Justification::centredRight);
    addAndMakeVisible(volumeValueLabel);

    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setRange(0.0, 150.0, 1.0);    // percent
    volumeSlider.setValue(100.0, juce::dontSendNotification);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(30, 20, 45));
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colours::hotpink.withAlpha(0.55f));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colours::hotpink);
    volumeSlider.addListener(this);
    addAndMakeVisible(volumeSlider);

    // BPM
    styleLabel(bpmLabel, 13.0f, true, juce::Colour(200, 160, 230),
        juce::Justification::centredLeft);
    bpmLabel.setText("BPM", juce::dontSendNotification);
    addAndMakeVisible(bpmLabel);

    styleLabel(bpmValueLabel, 13.0f, false, juce::Colours::white,
        juce::Justification::centredRight);
    addAndMakeVisible(bpmValueLabel);

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setRange(30.0, 522.0, 1.0);
    bpmSlider.setValue(120.0, juce::dontSendNotification);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    bpmSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(30, 20, 45));
    bpmSlider.setColour(juce::Slider::trackColourId, juce::Colours::hotpink.withAlpha(0.55f));
    bpmSlider.setColour(juce::Slider::thumbColourId, juce::Colours::hotpink);
    bpmSlider.addListener(this);
    addAndMakeVisible(bpmSlider);

    // Time Signature
    styleLabel(timeSigLabel, 13.0f, true, juce::Colour(200, 160, 230),
        juce::Justification::centredLeft);
    timeSigLabel.setText("Time Signature", juce::dontSendNotification);
    addAndMakeVisible(timeSigLabel);

    timeSigButton.setButtonText("4/4");
    timeSigButton.setColour(juce::TextButton::buttonColourId, juce::Colour(40, 25, 60));
    timeSigButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    timeSigButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    timeSigButton.onClick = [this]()
        {
            juce::PopupMenu menu;
            juce::StringArray sigs = {
                "2/4", "3/4", "4/4", "5/4", "6/4", "7/4",
                "3/8", "5/8", "6/8", "7/8", "9/8", "12/8",
                "2/2", "3/2", "4/2"
            };
            for (int i = 0; i < sigs.size(); ++i)
                menu.addItem(i + 1, sigs[i]);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(timeSigButton),
                [this, sigs](int result)
                {
                    if (result > 0)
                    {
                        juce::String picked = sigs[result - 1];
                        timeSigButton.setButtonText(picked);
                        if (onTimeSigChanged)
                            onTimeSigChanged(picked);
                    }
                });
        };
    addAndMakeVisible(timeSigButton);

    styleLabel(footerLabel, 11.0f, false, juce::Colour(150, 130, 180),
        juce::Justification::centred);
    footerLabel.setText("Press Esc or click outside this panel to close.",
        juce::dontSendNotification);
    addAndMakeVisible(footerLabel);
}

DAWComponent::ProjectSettingsOverlay::~ProjectSettingsOverlay()
{
    volumeSlider.removeListener(this);
    bpmSlider.removeListener(this);
}

void DAWComponent::ProjectSettingsOverlay::prime(const juce::String& projectName,
    int bpm,
    const juce::String& timeSig,
    float masterVolume01)
{
    nameEditor.setText(projectName, juce::dontSendNotification);

    const double v = juce::jlimit(0.0, 150.0, (double)(masterVolume01 * 100.0f));
    volumeSlider.setValue(v, juce::dontSendNotification);
    volumeValueLabel.setText(juce::String((int)std::round(v)) + " %",
        juce::dontSendNotification);

    const int b = juce::jlimit(30, 522, bpm);
    bpmSlider.setValue((double)b, juce::dontSendNotification);
    bpmValueLabel.setText(juce::String(b) + " BPM", juce::dontSendNotification);

    timeSigButton.setButtonText(timeSig.isNotEmpty() ? timeSig : juce::String("4/4"));

    repaint();
}

void DAWComponent::ProjectSettingsOverlay::sliderValueChanged(juce::Slider* s)
{
    if (s == &volumeSlider)
    {
        const int pct = (int)std::round(volumeSlider.getValue());
        volumeValueLabel.setText(juce::String(pct) + " %",
            juce::dontSendNotification);
        if (onMasterVolumeChanged)
            onMasterVolumeChanged((float)pct / 100.0f);
    }
    else if (s == &bpmSlider)
    {
        const int bpm = (int)std::round(bpmSlider.getValue());
        bpmValueLabel.setText(juce::String(bpm) + " BPM",
            juce::dontSendNotification);
        if (onBpmChanged)
            onBpmChanged(bpm);
    }
}

juce::Rectangle<int> DAWComponent::ProjectSettingsOverlay::getCardBounds() const
{
    const int w = juce::jmin(560, getWidth() - 80);
    const int h = juce::jmin(430, getHeight() - 60);
    return juce::Rectangle<int>((getWidth() - w) / 2,
        (getHeight() - h) / 2,
        w, h);
}

void DAWComponent::ProjectSettingsOverlay::paint(juce::Graphics& g)
{
    // Dim backdrop
    g.fillAll(juce::Colour(0, 0, 0).withAlpha(0.55f));

    auto card = getCardBounds().toFloat();

    juce::ColourGradient grad(juce::Colour(35, 25, 55),
        card.getCentreX(), card.getY(),
        juce::Colour(20, 15, 35),
        card.getCentreX(), card.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(card, 12.0f);

    g.setColour(juce::Colours::hotpink.withAlpha(0.6f));
    g.drawRoundedRectangle(card, 12.0f, 1.5f);

    const float divY = card.getY() + 52.0f;
    g.setColour(juce::Colours::hotpink.withAlpha(0.25f));
    g.drawLine(card.getX() + 14.0f, divY, card.getRight() - 14.0f, divY, 1.0f);
}

void DAWComponent::ProjectSettingsOverlay::resized()
{
    auto card = getCardBounds();

    // Header
    auto header = card.removeFromTop(52);
    closeButton.setBounds(header.removeFromRight(44).withSizeKeepingCentre(28, 28));
    titleLabel.setBounds(header.reduced(18, 0));

    // Body
    auto body = card.reduced(20, 14);

    // Footer reserved at bottom
    auto footer = body.removeFromBottom(20);
    footerLabel.setBounds(footer);
    body.removeFromBottom(6);

    const int rowH = 24;
    const int controlH = 28;
    const int sectionGap = 14;

    auto doRow = [&](juce::Label& heading, juce::Component* control,
        juce::Label* rightValue)
        {
            auto hr = body.removeFromTop(rowH);
            if (rightValue != nullptr)
            {
                rightValue->setBounds(hr.removeFromRight(90));
            }
            heading.setBounds(hr);

            if (control != nullptr)
                control->setBounds(body.removeFromTop(controlH));

            body.removeFromTop(sectionGap);
        };

    doRow(nameLabel, &nameEditor, nullptr);
    doRow(volumeLabel, &volumeSlider, &volumeValueLabel);
    doRow(bpmLabel, &bpmSlider, &bpmValueLabel);
    doRow(timeSigLabel, &timeSigButton, nullptr);
}

void DAWComponent::ProjectSettingsOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (!getCardBounds().contains(e.getPosition()))
        setVisible(false);
}

bool DAWComponent::ProjectSettingsOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        // Commit any in-progress name edit before dismissing
        if (nameEditor.hasKeyboardFocus(true) && onProjectNameCommitted)
            onProjectNameCommitted(nameEditor.getText());
        setVisible(false);
        return true;
    }
    return false;
}

void DAWComponent::ProjectSettingsOverlay::visibilityChanged()
{
    if (isVisible())
    {
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();
    }
}

void DAWComponent::exportTimelineAsWav()
{
    if (!isVocalBankReady.load())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Cannot Export Yet",
            "The voice bank is still loading. Please wait a moment, then try again.");
        return;
    }

    if (placedClips.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Nothing to Export",
            "Place some clips on the timeline first, then export.");
        return;
    }

    // ── Default save location: ~/Downloads, fall back to Documents ──────────
    juce::File downloads = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Downloads");
    if (!downloads.isDirectory())
        downloads = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    juce::String safeName = juce::File::createLegalFileName(currentProjectName);
    if (safeName.trim().isEmpty()) safeName = "VocalNite Export";
    juce::File defaultFile = downloads.getChildFile(safeName + ".wav");

    // shared_ptr keeps the chooser alive until the async callback fires.
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Project as WAV",
        defaultFile,
        "*.wav");

    auto* chooserPtr = chooser.get();
    chooserPtr->launchAsync(
        juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc)
        {
            if (isDying.load()) return;

            juce::File f = fc.getResult();
            if (f == juce::File()) return;        // user cancelled
            if (f.getFileExtension().isEmpty())
                f = f.withFileExtension(".wav");

            startExport(f);
        });
}

void DAWComponent::startExport(const juce::File& destFile)
{
    // ── Halt live transport so nothing fights the offline renderer ──────────
    if (isPlaying)
    {
        isPlaying = false;
        stopTimer();
    }
    vocalSynth.setPaused(false);
    vocalSynth.stop();
    vocalSynth.setMetronomeEnabled(false);
    playheadPosition = 0.0;
    lastTriggeredBeat = -1;

    // ── Detach engine from the audio device ─────────────────────────────────
    // Until we re-attach in onExportFinished, the audio thread will not call
    // vocalSynth.getNextAudioBlock(). The export thread has exclusive access.
    audioDeviceManager.removeAudioCallback(&synthPlayer);
    synthPlayer.setSource(nullptr);

    // Dim transport buttons during export (gates on isVocalBankReady, same
    // as the voice-bank-load and voice-bank-swap flows).
    isVocalBankReady.store(false);
    refreshTransportEnabled();

    // Show the existing loading overlay with a render-specific status message.
    loadingOverlay.setStatus("Rendering project to WAV...");
    loadingOverlay.setVisible(true);
    loadingOverlay.toFront(false);
    repaint();

    // ── Compute timeline length (highest clip end across all tracks) ────────
    double maxBeat = 0.0;
    for (const auto& c : placedClips)
        maxBeat = std::max(maxBeat, c.startBeat + c.duration);

    // ── Snapshot what the renderer needs ────────────────────────────────────
    // The render runs on a worker thread; copying these arrays now means the
    // user can keep editing clips/patterns during export with zero races.
    juce::Array<PlacedClip>            clipsCopy = placedClips;
    juce::Array<juce::Array<FullNote>> fullNotesCopy = patternFullNotes;
    const int                          bpmCopy = currentBPM;

    // Stop any prior export thread defensively (shouldn't normally happen).
    if (exportThread != nullptr)
    {
        exportThread->stopThread(3000);
        exportThread.reset();
    }

    exportThread.reset(new ExportThread(*this, destFile,
        std::move(clipsCopy),
        std::move(fullNotesCopy),
        bpmCopy,
        maxBeat));
    exportThread->startThread();
}

void DAWComponent::onExportFinished(bool success, const juce::File& destFile)
{
    if (isDying.load()) return;

    // ── Restore engine to live operation ────────────────────────────────────
    vocalSynth.stop();
    vocalSynth.setPaused(false);
    vocalSynth.setMetronomeEnabled(false);

    if (auto* dev = audioDeviceManager.getCurrentAudioDevice())
        vocalSynth.prepareToPlay(dev->getCurrentBufferSizeSamples(),
            dev->getCurrentSampleRate());

    synthPlayer.setSource(&vocalSynth);
    audioDeviceManager.addAudioCallback(&synthPlayer);

    // Re-enable transport + hide overlay
    isVocalBankReady.store(true);
    refreshTransportEnabled();
    loadingOverlay.setVisible(false);
    repaint();

    if (success)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Export Complete",
            "Saved to:\n" + destFile.getFullPathName());
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Export Failed",
            "Could not write to:\n" + destFile.getFullPathName()
            + "\n\nCheck that the folder exists and isn't read-only.");
    }
}

// ============================================================================
//  ExportThread — offline render loop
// ============================================================================
//
//  Mirrors the live engine's "integer-beat crossing" trigger semantics:
//  every time the chunk's start sample crosses into a new integer beat,
//  fire the pattern notes at that beat (queueLyric → engine pendingVoices).
//
//  Audio is pulled via vocalSynth.getNextAudioBlock() in 1024-sample chunks.
//  We keep rendering for `tailSeconds` after the last clip ends so any
//  release/decay tails are captured. Output is clamped to [-1, 1] before
//  hand-off to WavAudioFormat (which converts to int16).

void DAWComponent::ExportThread::run()
{
    if (threadShouldExit())
    {
        juce::MessageManager::callAsync(
            [ownerPtr = &owner, file = destFile]()
            { ownerPtr->onExportFinished(false, file); });
        return;
    }

    // ── Render parameters ───────────────────────────────────────────────────
    const double sampleRate = 44100.0;
    const int    blockSize = 1024;
    const int    bitDepth = 16;
    const int    numChannels = 2;
    const double tailSeconds = 2.0;

    const double safeBpm = (bpm > 0) ? (double)bpm : 120.0;
    const double secondsPerBeat = 60.0 / safeBpm;
    const double totalSeconds = maxBeat * secondsPerBeat + tailSeconds;
    const int    totalSamples = std::max(1, (int)std::ceil(totalSeconds * sampleRate));

    // ── Reset engine for offline rendering ──────────────────────────────────
    // The audio device is detached (startExport did that), so we have
    // exclusive access. prepareToPlay reconfigures click buffers etc. for
    // the export sample rate.
    owner.vocalSynth.stop();
    owner.vocalSynth.setPaused(false);
    owner.vocalSynth.setMetronomeEnabled(false);
    owner.vocalSynth.prepareToPlay(blockSize, sampleRate);

    // ── Allocate output buffer + render in chunks ───────────────────────────
    juce::AudioBuffer<float> outBuf(numChannels, totalSamples);
    outBuf.clear();

    int lastTriggered = -1;
    int produced = 0;

    while (produced < totalSamples)
    {
        if (threadShouldExit()) break;

        const int chunk = std::min(blockSize, totalSamples - produced);

        // Beat at the START of this chunk. Mirrors the live timer's
        // (int)playheadPosition > lastTriggeredBeat trigger logic.
        const double chunkStartBeat = ((double)produced / sampleRate) / secondsPerBeat;
        const int    beatInt = (int)std::floor(chunkStartBeat);

        if (beatInt > lastTriggered)
        {
            for (int b = lastTriggered + 1; b <= beatInt; ++b)
            {
                if ((double)b >= maxBeat) break;   // past song end → tail period only
                triggerForBeat(b);
            }
            lastTriggered = beatInt;
        }

        juce::AudioSourceChannelInfo info;
        info.buffer = &outBuf;
        info.startSample = produced;
        info.numSamples = chunk;
        owner.vocalSynth.getNextAudioBlock(info);

        produced += chunk;
    }

    // ── Clamp output to [-1, 1] to avoid wraparound in 16-bit conversion ────
    for (int ch = 0; ch < outBuf.getNumChannels(); ++ch)
    {
        juce::FloatVectorOperations::clip(
            outBuf.getWritePointer(ch),
            outBuf.getReadPointer(ch),
            -1.0f, 1.0f, totalSamples);
    }

    // ── Write the WAV ───────────────────────────────────────────────────────
    bool ok = false;
    {
        if (destFile.exists())
            destFile.deleteFile();

        std::unique_ptr<juce::FileOutputStream> fos = destFile.createOutputStream();
        if (fos != nullptr)
        {
            // FileOutputStream defaults to read-only on some platforms after
            // failure of an earlier open — explicitly position at zero.
            fos->setPosition(0);
            fos->truncate();

            juce::WavAudioFormat wav;
            // createWriterFor takes ownership of the stream regardless of
            // success (deletes it on failure). We release() before the call.
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(fos.get(), sampleRate,
                    (unsigned int)numChannels, bitDepth, {}, 0));

            if (writer != nullptr)
            {
                fos.release();          // writer now owns the stream
                ok = writer->writeFromAudioSampleBuffer(outBuf, 0, totalSamples);
                // writer dtor flushes & closes the file
            }
        }
    }

    // ── Post completion to the message thread ───────────────────────────────
    juce::MessageManager::callAsync(
        [ownerPtr = &owner, file = destFile, ok]()
        { ownerPtr->onExportFinished(ok, file); });
}

void DAWComponent::ExportThread::triggerForBeat(int globalBeat)
{
    // Mirrors DAWComponent::triggerNotesAtBeat — but reads only from local
    // snapshots so the user can edit clips/patterns mid-export without races.
    for (const auto& clip : clipsCopy)
    {
        const int localBeat = globalBeat - (int)clip.startBeat;
        if (localBeat < 0 || localBeat >= (int)clip.duration) continue;

        const int pIdx = clip.patternIndex;
        if (pIdx < 0 || pIdx >= fullNotesCopy.size()) continue;

        for (const auto& note : fullNotesCopy.getReference(pIdx))
        {
            if (note.beat == localBeat && note.lyric.isNotEmpty())
                owner.vocalSynth.queueLyric(note.lyric, note.pitch,
                    (double)bpm, (double)note.duration);
        }
    }
}