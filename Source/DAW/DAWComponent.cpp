#include <JuceHeader.h>
#include "DAWComponent.h"

DAWComponent::DAWComponent(const juce::String& projectName)
    : menuBar(this), currentProjectName(projectName)
{
    // Menu bar
    addAndMakeVisible(menuBar);

    // Logo placeholder
    logoLabel.setText("VocalNite", juce::dontSendNotification);
    logoLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    logoLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    addAndMakeVisible(logoLabel);

    // Project name
    projectNameLabel.setText(currentProjectName, juce::dontSendNotification);
    projectNameLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    projectNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    projectNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(projectNameLabel);

    // Back button
    backButton.setButtonText("< Dashboard");
    backButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 0, 90));
    backButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(backButton);

    // Transport buttons
    playButton.setButtonText(">");
    pauseButton.setButtonText("||");
    stopButton.setButtonText("[]");
    skipButton.setButtonText(">|");

    for (auto* btn : { &playButton, &pauseButton, &stopButton, &skipButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    // Mode buttons
    selectModeButton.setButtonText("Select");
    editModeButton.setButtonText("Edit");

    for (auto* btn : { &selectModeButton, &editModeButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0, 60, 120));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    // Piano roll toggle button
    pianoRollButton.setButtonText("Piano Roll");
    pianoRollButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 0, 120));
    pianoRollButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    pianoRollButton.onClick = [this]()
        {
            pianoRollVisible = !pianoRollVisible;
            pianoRoll.setVisible(pianoRollVisible);
            resized();
        };
    addAndMakeVisible(pianoRollButton);
    addChildComponent(pianoRoll);

    // Tempo and time signature
    tempoButton.setButtonText("BPM: 120");
    timeSigButton.setButtonText("4/4");

    for (auto* btn : { &tempoButton, &timeSigButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(20, 80, 20));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(btn);
    }

    // Lyrics input
    lyricsLabel.setText("Lyrics:", juce::dontSendNotification);
    lyricsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(lyricsLabel);

    lyricsInput.setMultiLine(false);
    lyricsInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(30, 30, 50));
    lyricsInput.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(lyricsInput);

    setSize(1280, 720);
}

DAWComponent::~DAWComponent() {}

void DAWComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(15, 15, 25));

    // Draw grid
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int gridLeft = 120;
    int cellWidth = 80;
    int cellHeight = 40;
    int numCols = (getWidth() - gridLeft) / cellWidth + 1;
    int numRows = (getHeight() - gridTop) / cellHeight + 1;

    // Track rows background alternating
    for (int row = 0; row < numRows; ++row)
    {
        if (row % 2 == 0)
            g.setColour(juce::Colour(25, 25, 40));
        else
            g.setColour(juce::Colour(20, 20, 35));
        g.fillRect(gridLeft, gridTop + row * cellHeight, getWidth() - gridLeft, cellHeight);
    }

    // Grid lines
    g.setColour(juce::Colour(60, 60, 90));
    for (int col = 0; col <= numCols; ++col)
        g.drawLine(gridLeft + col * cellWidth, gridTop, gridLeft + col * cellWidth, getHeight(), 1.0f);
    for (int row = 0; row <= numRows; ++row)
        g.drawLine(gridLeft, gridTop + row * cellHeight, getWidth(), gridTop + row * cellHeight, 1.0f);

    // Measure numbers
    g.setColour(juce::Colours::grey);
    g.setFont(12.0f);
    for (int col = 0; col < numCols; ++col)
        g.drawText(juce::String(col + 1), gridLeft + col * cellWidth + 4, gridTop - 18, cellWidth, 16, juce::Justification::left);

    // Track labels
    for (int row = 0; row < numRows; ++row)
        g.drawText("Track " + juce::String(row + 1), 4, gridTop + row * cellHeight, gridLeft - 8, cellHeight, juce::Justification::centredRight);

    // Toolbar backgrounds
    g.setColour(juce::Colour(30, 10, 50));
    g.fillRect(0, menuBarHeight, getWidth(), toolbarHeight);
    g.setColour(juce::Colour(20, 20, 40));
    g.fillRect(0, menuBarHeight + toolbarHeight, getWidth(), toolbar2Height);
}

void DAWComponent::resized()
{
    int menuBarHeight = 25;
    int toolbarHeight = 40;
    int toolbar2Height = 35;
    int y1 = menuBarHeight;
    int y2 = y1 + toolbarHeight;

    menuBar.setBounds(0, 0, getWidth(), menuBarHeight);

    // Toolbar 1
    logoLabel.setBounds(4, y1 + 5, 100, 30);
    backButton.setBounds(110, y1 + 5, 110, 30);
    projectNameLabel.setBounds(getWidth() / 2 - 150, y1 + 5, 300, 30);
    selectModeButton.setBounds(getWidth() - 310, y1 + 5, 70, 30);
    editModeButton.setBounds(getWidth() - 235, y1 + 5, 60, 30);
    pianoRollButton.setBounds(getWidth() - 170, y1 + 5, 90, 30);

    // Toolbar 2
    tempoButton.setBounds(4, y2 + 4, 90, 26);
    timeSigButton.setBounds(100, y2 + 4, 60, 26);
    lyricsLabel.setBounds(170, y2 + 4, 55, 26);
    lyricsInput.setBounds(225, y2 + 4, 300, 26);

    int btnSize = 28;
    int btnY = y2 + 3;
    int btnStartX = getWidth() - (btnSize + 4) * 4 - 4;
    playButton.setBounds(btnStartX, btnY, btnSize, btnSize);
    pauseButton.setBounds(btnStartX + btnSize + 4, btnY, btnSize, btnSize);
    stopButton.setBounds(btnStartX + (btnSize + 4) * 2, btnY, btnSize, btnSize);
    skipButton.setBounds(btnStartX + (btnSize + 4) * 3, btnY, btnSize, btnSize);

    // Grid and piano roll area
    int gridTop = menuBarHeight + toolbarHeight + toolbar2Height;
    int pianoRollHeight = pianoRollVisible ? 250 : 0;
    int gridHeight = getHeight() - gridTop - pianoRollHeight;

    if (pianoRollVisible)
        pianoRoll.setBounds(0, getHeight() - pianoRollHeight, getWidth(), pianoRollHeight);
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
        menu.addItem(1, "New Project");
        menu.addItem(2, "Open Project");
        menu.addItem(3, "Save Project");
        menu.addSeparator();
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
        menu.addItem(13, "Settings");
    }
    else if (menuIndex == 6) // Help
    {
        menu.addItem(14, "About VocalNite");
    }
    return menu;
}

void DAWComponent::menuItemSelected(int menuItemID, int)
{
    // menu actions will be implemented later
}