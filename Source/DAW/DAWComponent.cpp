#include <JuceHeader.h>
#include "DAWComponent.h"
#include "../Database/DatabaseManager.h"

class PatternEditorWindow : public juce::DocumentWindow
{
public:
    PatternEditorWindow(const juce::String& title)
        : DocumentWindow(title, juce::Colour(15, 15, 25), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizable(false, false);
    }

    void closeButtonPressed() override
    {
        delete this;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternEditorWindow)
};

DAWComponent::DAWComponent(const juce::String& projectName, int projectId, const juce::String& username)
    : menuBar(this), currentProjectName(projectName), currentProjectId(projectId), currentUsername(username)
{
    // Menu bar
    addAndMakeVisible(menuBar);

    // Logo placeholder
    logoLabel.setText("VocalNite", juce::dontSendNotification);
    logoLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    logoLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    addAndMakeVisible(logoLabel);

    // Username label
    usernameLabel.setFont(juce::Font(13.0f));
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

    // Transport buttons
    playButton.setButtonText(">");
    pauseButton.setButtonText("||");
    stopButton.setButtonText("[]");

    for (auto* btn : { &playButton, &pauseButton, &stopButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    playButton.onClick = [this]()
        {
            isPlaying = true;
            startTimer(1000 / 60);
        };

    pauseButton.onClick = [this]()
        {
            isPlaying = false;
            stopTimer();
            vocalSynth.stop();
        };

    stopButton.onClick = [this]()
        {
            isPlaying = false;
            stopTimer();
            playheadPosition = 0.0;
            lastTriggeredBeat = -1;
            vocalSynth.stop();
            repaint();
        };

    // Mode buttons
    selectModeButton.setButtonText("Select");
    editModeButton.setButtonText("Edit");

    for (auto* btn : { &selectModeButton, &editModeButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0, 60, 120));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    // Metronome button
    metronomeButton.setButtonText("Metro");
    metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
    metronomeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    metronomeButton.onClick = [this]()
        {
            metronomeEnabled = !metronomeEnabled;
            vocalSynth.setMetronomeEnabled(metronomeEnabled);
            metronomeButton.setColour(juce::TextButton::buttonColourId,
                metronomeEnabled ? juce::Colour(0, 120, 80) : juce::Colour(40, 40, 60));
        };
    addAndMakeVisible(metronomeButton);

    // Initialize audio device
    audioDeviceManager.initialiseWithDefaultDevices(0, 2);

    // ── Load vocal synthesis resources ──
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

    if (resourcesDir == juce::File())
    {
        DBG("ERROR: Could not locate Resources folder!");
    }
    else
    {
        vocalSynth.loadDictionary(resourcesDir.getChildFile("cmudict.txt"));
        vocalSynth.loadVoiceBank(resourcesDir.getChildFile("VoiceBank"));
    }

    synthPlayer.setSource(&vocalSynth);
    audioDeviceManager.addAudioCallback(&synthPlayer);

    // Sync initial tempo and time signature to the engine
    vocalSynth.setTempo((double)currentBPM);
    vocalSynth.setTimeSignature(4, 4);

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
    patternRenameEditor.onReturnKey = [this]()
        {
            if (editingPatternIndex >= 0 && editingPatternIndex < patternNames.size())
            {
                juce::String newName = patternRenameEditor.getText();
                patternNames.set(editingPatternIndex, newName);

                if (editingPatternIndex < patternIds.size() && patternIds[editingPatternIndex] >= 0)
                {
                    try
                    {
                        SQLite::Statement query(DatabaseManager::get().db(),
                            "UPDATE Patterns SET name = ? WHERE pattern_id = ?");
                        query.bind(1, newName.toStdString());
                        query.bind(2, patternIds[editingPatternIndex]);
                        query.exec();
                    }
                    catch (const std::exception& e)
                    {
                        DBG("Pattern rename error: " + juce::String(e.what()));
                    }
                }

                editingPatternIndex = -1;
                patternRenameEditor.setVisible(false);
                repaint();
            }
        };
    addAndMakeVisible(patternRenameEditor);

    // Dynamic tracks
    trackNames.add("Track 1");
    trackNames.add("Track 2");
    trackNames.add("Track 3");
    trackNames.add("Track 4");
    trackNames.add("Track 5");

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
    loadPatternNotes();
    loadFullPatternNotes();
    setSize(1280, 720);
}

DAWComponent::~DAWComponent()
{
    audioDeviceManager.removeAudioCallback(&synthPlayer);
    synthPlayer.setSource(nullptr);
}

void DAWComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(15, 15, 25));

    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int patternWidth = 150;
    int trackHeaderWidth = 80;
    int gridLeft = patternWidth + trackHeaderWidth;
    int cellWidth = 80;
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

        g.setColour(i % 2 == 0 ? juce::Colour(25, 25, 40) : juce::Colour(20, 20, 35));
        g.fillRect(gridLeft, y, gridWidth, trackHeight);

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

        g.setFont(12.0f);
        g.drawText(trackNames[i], patternWidth + 26, y, trackHeaderWidth - 28, trackHeight,
            juce::Justification::centredLeft);

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

                int pIdx = clip.patternIndex;
                if (pIdx < patternNotePreviews.size())
                {
                    auto& notes = patternNotePreviews.getReference(pIdx);
                    int maxBeat = 32;
                    int minPitch = 127, maxPitch = 0;

                    for (auto& note : notes)
                    {
                        minPitch = std::min(minPitch, note.pitch);
                        maxPitch = std::max(maxPitch, note.pitch);
                    }
                    int pitchRange = std::max(maxPitch - minPitch, 12);

                    int innerTop = y + 14;
                    int innerBottom = y + trackHeight - 3;
                    int innerHeight = innerBottom - innerTop;
                    int barH = std::max(2, innerHeight / pitchRange);

                    for (auto& note : notes)
                    {
                        float noteXRatio = (float)note.beat / maxBeat;
                        float noteYRatio = 1.0f - (float)(note.pitch - minPitch) / pitchRange;
                        int nx = clipX + 2 + (int)(noteXRatio * (clipW - 4));
                        int ny = innerTop + (int)(noteYRatio * (innerHeight - barH));
                        int barW = std::max(3, (clipW - 4) / maxBeat);

                        if (nx >= clipX && nx + barW <= clipX + clipW)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.55f));
                            g.fillRect(nx, ny, barW, barH);
                        }
                    }
                }

                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.setFont(11.0f);
                g.drawText(patternNames[clip.patternIndex], clipX + 4, y + 2,
                    clipW - 8, trackHeight - 4, juce::Justification::centredLeft);
                g.setColour(juce::Colour(140, 60, 200));
                g.drawRoundedRectangle(clipX + 1, y + 2, clipW - 2, trackHeight - 4, 4.0f, 1.0f);
            }
        }

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
        g.drawText(trackNames[i], patternWidth + 26, y, trackHeaderWidth - 28, trackHeight,
            juce::Justification::centredLeft);
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
        g.drawText(patternNames[draggingPatternIndex], dragX + 4, dragY - patternHeight / 2,
            140, patternHeight - 4, juce::Justification::centredLeft);
    }
    else if (isDraggingClip && draggingClipIndex >= 0 && draggingClipIndex < placedClips.size())
    {
        auto& clip = placedClips.getReference(draggingClipIndex);
        int clipW = (int)(clip.duration * cellWidth);
        g.setColour(juce::Colour(100, 40, 160).withAlpha(0.7f));
        g.fillRoundedRectangle(dragX, dragY - trackHeight / 2, clipW, trackHeight - 4, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(11.0f);
        g.drawText(patternNames[clip.patternIndex], dragX + 4, dragY - trackHeight / 2,
            clipW - 8, trackHeight - 4, juce::Justification::centredLeft);
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

    logoLabel.setBounds(4, y1 + 5, 100, 30);
    projectNameLabel.setBounds(getWidth() / 2 - 100, y1 + 5, 200, 30);
    usernameLabel.setBounds(getWidth() - 260, y1 + 5, 130, 30);
    selectModeButton.setBounds(getWidth() - 155, y1 + 5, 70, 30);
    editModeButton.setBounds(getWidth() - 80, y1 + 5, 70, 30);

    tempoButton.setBounds(4, y2 + 4, 90, 26);
    timeSigButton.setBounds(100, y2 + 4, 60, 26);

    int btnSize = 28;
    int btnY = y2 + 3;
    int btnStartX = 170;
    playButton.setBounds(btnStartX, btnY, btnSize, btnSize);
    pauseButton.setBounds(btnStartX + btnSize + 4, btnY, btnSize, btnSize);
    stopButton.setBounds(btnStartX + (btnSize + 4) * 2, btnY, btnSize, btnSize);
    metronomeButton.setBounds(btnStartX + (btnSize + 4) * 3, btnY, 50, btnSize);

    addTrackButton.setBounds(4, getHeight() - 30, 120, 24);

    int pianoRollHeight = pianoRollVisible ? 250 : 0;
    int trackAreaHeight = getHeight() - gridTop - pianoRollHeight - 30;
    trackScrollBar.setBounds(getWidth() - scrollBarWidth, gridTop, scrollBarWidth, trackAreaHeight);

    int totalTrackHeight = trackNames.size() * trackHeight;
    trackScrollBar.setRangeLimits(0.0, totalTrackHeight);
    trackScrollBar.setCurrentRange(trackScrollOffset, trackAreaHeight);

    if (pianoRollVisible)
        pianoRoll.setBounds(0, getHeight() - pianoRollHeight - 30, getWidth(), pianoRollHeight);

    addPatternButton.setBounds(4, getHeight() - 60, 140, 24);

    int patternWidth = 150;
    int patternAreaHeight = getHeight() - gridTop - 60;
    patternScrollBar.setBounds(patternWidth - 10, gridTop + 20, 10, patternAreaHeight);
    int totalPatternHeight = patternNames.size() * patternHeight + patternHeight;
    patternScrollBar.setRangeLimits(0.0, totalPatternHeight);
    patternScrollBar.setCurrentRange(patternScrollOffset, patternAreaHeight);

    int gridLeft = patternWidth + 80;
    int horizontalScrollBarY = getHeight() - 30;
    horizontalScrollBar.setBounds(gridLeft, horizontalScrollBarY, getWidth() - gridLeft - scrollBarWidth, 12);
    int totalGridWidth = 128 * 80;
    horizontalScrollBar.setRangeLimits(0.0, totalGridWidth);
    horizontalScrollBar.setCurrentRange(horizontalScrollOffset, getWidth() - gridLeft);
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
        menu.addItem(14, "About VocalNite");
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

    case 10: // Play
        isPlaying = true;
        startTimer(1000 / 60);
        break;

    case 11: // Pause
        isPlaying = false;
        stopTimer();
        vocalSynth.stop();
        break;

    case 12: // Stop
        isPlaying = false;
        stopTimer();
        playheadPosition = 0.0;
        lastTriggeredBeat = -1;
        vocalSynth.stop();
        repaint();
        break;

    case 13: // Metronome
        metronomeEnabled = !metronomeEnabled;
        vocalSynth.setMetronomeEnabled(metronomeEnabled);
        metronomeButton.setColour(juce::TextButton::buttonColourId,
            metronomeEnabled ? juce::Colour(0, 120, 80) : juce::Colour(40, 40, 60));
        break;

    case 14: // Export As
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

    Action action;
    action.type = Action::AddTrack;
    action.trackName = newName;
    action.trackIndex = trackNames.size() - 1;
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
        undoStack.add(action);
        redoStack.clear();
        if (undoStack.size() > 10) undoStack.remove(0);

        trackNames.remove(index);
        int totalHeight = trackNames.size() * trackHeight;
        trackScrollBar.setRangeLimits(0.0, totalHeight);
        resized();
        repaint();
    }
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
                            int menuBarHeight = 25;
                            int toolbarHeight = 40;
                            int toolbar2Height = 35;
                            int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
                            int patternAreaTop = gridTop + 28;
                            int gridLeft = 200;
                            int y = patternAreaTop + i * patternHeight;
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
                                try
                                {
                                    SQLite::Statement query(DatabaseManager::get().db(),
                                        "INSERT INTO Patterns (project_id, name) VALUES (?, ?)");
                                    query.bind(1, currentProjectId);
                                    query.bind(2, copyName.toStdString());
                                    query.exec();
                                    newPatternId = (int)DatabaseManager::get().db().getLastInsertRowid();

                                    if (i < patternIds.size() && patternIds[i] >= 0)
                                    {
                                        SQLite::Statement notesQuery(DatabaseManager::get().db(),
                                            "INSERT INTO PatternNotes (pattern_id, pitch, beat, lyric) "
                                            "SELECT ?, pitch, beat, lyric FROM PatternNotes WHERE pattern_id = ?");
                                        notesQuery.bind(1, newPatternId);
                                        notesQuery.bind(2, patternIds[i]);
                                        notesQuery.exec();
                                    }
                                }
                                catch (const std::exception& e)
                                {
                                    DBG("Pattern copy error: " + juce::String(e.what()));
                                }
                            }

                            patternNames.add(copyName);
                            patternIds.add(newPatternId);
                            loadFullPatternNotes();
                            repaint();
                            resized();
                        }
                        else if (result == 3)
                        {
                            if (i < patternIds.size() && patternIds[i] >= 0)
                            {
                                try
                                {
                                    SQLite::Statement query(DatabaseManager::get().db(),
                                        "DELETE FROM Patterns WHERE pattern_id = ?");
                                    query.bind(1, patternIds[i]);
                                    query.exec();
                                }
                                catch (const std::exception& e)
                                {
                                    DBG("Pattern delete error: " + juce::String(e.what()));
                                }
                            }
                            Action action;
                            action.type = Action::RemovePattern;
                            action.patternName = patternNames[i];
                            action.patternId = patternIds[i];
                            action.patternIndex = i;
                            undoStack.add(action);
                            redoStack.clear();
                            if (undoStack.size() > 10) undoStack.remove(0);

                            patternNames.remove(i);
                            patternIds.remove(i);
                            loadFullPatternNotes();
                            repaint();
                            resized();
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
    int patternWidth = 150;
    int trackHeaderWidth = 80;
    int gridLeft = patternWidth + trackHeaderWidth;
    int cellWidth = 80;
    int scrollBarWidth = 12;
    int gridWidth = getWidth() - gridLeft - scrollBarWidth;
    int scrolledX = (int)horizontalScrollOffset;

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

    if (currentProjectId >= 0)
    {
        try
        {
            SQLite::Statement query(DatabaseManager::get().db(),
                "INSERT INTO Patterns (project_id, name) VALUES (?, ?)");
            query.bind(1, currentProjectId);
            query.bind(2, newName.toStdString());
            query.exec();
            newPatternId = (int)DatabaseManager::get().db().getLastInsertRowid();
            DBG("Pattern saved with ID: " + juce::String(newPatternId));
        }
        catch (const std::exception& e)
        {
            DBG("Pattern save error: " + juce::String(e.what()));
        }
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
}

void DAWComponent::openPatternEditor(int index)
{
    int patternId = (index < patternIds.size()) ? patternIds[index] : -1;
    auto* window = new PatternEditorWindow("Pattern Editor: " + patternNames[index]);
    auto* roll = new PianoRollComponent(patternId);
    window->setContentOwned(roll, true);
    window->centreWithSize(1280, 720);
    window->setVisible(true);

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
        g.drawText(patternNames[i], 10, y, patternWidth - 24, patternHeight - 4,
            juce::Justification::centredLeft);
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

        // Metronome visual flash (audio is handled by VocalSynthEngine)
        if (metronomeEnabled && vocalSynth.didMetronomeTick())
        {
            metronomeBeat = true;
            vocalSynth.clearMetronomeTick();
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
        int cellWidth = 80;

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
    int cellWidth = 80;

    if (isDraggingClip && draggingClipIndex >= 0)
    {
        if (e.x < patternWidth)
        {
            Action action;
            action.type = Action::RemoveClip;
            action.clip = placedClips[draggingClipIndex];
            action.clipIndex = draggingClipIndex;
            undoStack.add(action);
            redoStack.clear();
            if (undoStack.size() > 10) undoStack.remove(0);

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
                        double nearestBeat = std::round(beat);
                        if (std::abs(beat - nearestBeat) < snapThreshold)
                            beat = nearestBeat;

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
                    break;
                }
            }
        }
    }
    else if (isDraggingPattern && draggingPatternIndex >= 0)
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
                    double nearestBeat = std::round(beat);
                    if (std::abs(beat - nearestBeat) < snapThreshold)
                        beat = nearestBeat;

                    for (auto& clip : placedClips)
                    {
                        if (clip.trackIndex == i)
                        {
                            if (std::abs(beat - (clip.startBeat + clip.duration)) < snapThreshold)
                                beat = clip.startBeat + clip.duration;
                            else if (std::abs(beat - clip.startBeat) < snapThreshold)
                                beat = clip.startBeat - 4.0;
                        }
                    }
                }

                PlacedClip clip;
                clip.patternIndex = draggingPatternIndex;
                clip.trackIndex = i;
                clip.startBeat = beat;
                clip.duration = (draggingPatternIndex < patternDurations.size())
                    ? patternDurations[draggingPatternIndex]
                    : 4.0;
                placedClips.add(clip);

                Action action;
                action.type = Action::AddClip;
                action.clip = clip;
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
        SQLite::Statement query(DatabaseManager::get().db(),
            "SELECT pattern_id, name FROM Patterns WHERE project_id = ? ORDER BY pattern_id ASC");
        query.bind(1, currentProjectId);

        while (query.executeStep())
        {
            int id = query.getColumn(0).getInt();
            juce::String name = juce::String(query.getColumn(1).getString());
            patternIds.add(id);
            patternNames.add(name);
        }

        DBG("Loaded " + juce::String(patternNames.size()) + " patterns");
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

    switch (action.type)
    {
    case Action::AddPattern:
        if (action.patternId >= 0)
        {
            try
            {
                SQLite::Statement query(DatabaseManager::get().db(),
                    "DELETE FROM Patterns WHERE pattern_id = ?");
                query.bind(1, action.patternId);
                query.exec();
            }
            catch (...) {}
        }
        patternNames.remove(action.patternIndex);
        patternIds.remove(action.patternIndex);
        break;

    case Action::RemovePattern:
        patternNames.insert(action.patternIndex, action.patternName);
        patternIds.insert(action.patternIndex, action.patternId);
        if (action.patternId >= 0)
        {
            try
            {
                SQLite::Statement query(DatabaseManager::get().db(),
                    "INSERT INTO Patterns (pattern_id, project_id, name) VALUES (?, ?, ?)");
                query.bind(1, action.patternId);
                query.bind(2, currentProjectId);
                query.bind(3, action.patternName.toStdString());
                query.exec();
            }
            catch (...) {}
        }
        break;

    case Action::AddClip:
        placedClips.remove(action.clipIndex);
        break;

    case Action::RemoveClip:
        placedClips.insert(action.clipIndex, action.clip);
        break;

    case Action::MoveClip:
        placedClips.getReference(action.clipIndex) = action.previousClip;
        break;

    case Action::AddTrack:
        trackNames.remove(action.trackIndex);
        break;

    case Action::RemoveTrack:
        trackNames.insert(action.trackIndex, action.trackName);
        break;
    }

    redoStack.add(action);
    if (undoStack.size() > 10)
        undoStack.remove(0);

    loadFullPatternNotes();
    repaint();
    resized();
}

void DAWComponent::performRedo()
{
    if (redoStack.isEmpty()) return;

    Action action = redoStack.getLast();
    redoStack.removeLast();

    switch (action.type)
    {
    case Action::AddPattern:
        patternNames.insert(action.patternIndex, action.patternName);
        patternIds.insert(action.patternIndex, action.patternId);
        break;

    case Action::RemovePattern:
        patternNames.remove(action.patternIndex);
        patternIds.remove(action.patternIndex);
        break;

    case Action::AddClip:
        placedClips.insert(action.clipIndex, action.clip);
        break;

    case Action::RemoveClip:
        placedClips.remove(action.clipIndex);
        break;

    case Action::MoveClip:
        placedClips.getReference(action.clipIndex) = action.clip;
        break;

    case Action::AddTrack:
        trackNames.insert(action.trackIndex, action.trackName);
        break;

    case Action::RemoveTrack:
        trackNames.remove(action.trackIndex);
        break;
    }

    undoStack.add(action);
    loadFullPatternNotes();
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
            try
            {
                SQLite::Statement query(DatabaseManager::get().db(),
                    "SELECT pitch, beat FROM PatternNotes WHERE pattern_id = ? ORDER BY beat ASC");
                query.bind(1, patternId);
                while (query.executeStep())
                {
                    NotePreview n;
                    n.pitch = query.getColumn(0).getInt();
                    n.beat = query.getColumn(1).getInt();
                    notes.add(n);
                }
            }
            catch (const std::exception& e)
            {
                DBG("Load pattern notes error: " + juce::String(e.what()));
            }
        }
        patternNotePreviews.add(notes);
    }

    // Rebuild pattern durations from note data
    patternDurations.clear();
    for (int i = 0; i < patternNotePreviews.size(); ++i)
        patternDurations.add(getPatternDuration(i));
}

void DAWComponent::loadFullPatternNotes()
{
    patternFullNotes.clear();
    for (int i = 0; i < patternIds.size(); ++i)
    {
        juce::Array<FullNote> notes;
        int patId = patternIds[i];
        if (patId >= 0)
        {
            try
            {
                SQLite::Statement q(DatabaseManager::get().db(),
                    "SELECT pitch, beat, lyric FROM PatternNotes "
                    "WHERE pattern_id = ? ORDER BY beat ASC");
                q.bind(1, patId);
                while (q.executeStep())
                {
                    FullNote n;
                    n.pitch = q.getColumn(0).getInt();
                    n.beat = q.getColumn(1).getInt();
                    n.lyric = juce::String(q.getColumn(2).getString());
                    notes.add(n);
                }
            }
            catch (const std::exception& e)
            {
                DBG("loadFullPatternNotes error: " + juce::String(e.what()));
            }
        }
        patternFullNotes.add(notes);
    }

    // Update durations of already-placed clips to reflect edited patterns
    for (auto& clip : placedClips)
    {
        if (clip.patternIndex < patternDurations.size())
            clip.duration = patternDurations[clip.patternIndex];
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
                vocalSynth.queueLyric(note.lyric, note.pitch, (double)currentBPM);
        }
    }
}

double DAWComponent::getPatternDuration(int patternIndex) const
{
    double maxBeat = 0.0;

    if (patternIndex < patternNotePreviews.size())
    {
        for (const auto& note : patternNotePreviews.getReference(patternIndex))
            maxBeat = std::max(maxBeat, (double)(note.beat + 1));
    }

    // Round up to nearest 4 beats (one measure in 4/4) with a minimum of 4
    double duration = std::max(4.0, std::ceil(maxBeat / 4.0) * 4.0);
    return duration;
}