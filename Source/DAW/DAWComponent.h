#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

class DAWComponent : public juce::Component,
    public juce::MenuBarModel
{
public:
    DAWComponent(const juce::String& projectName);
    ~DAWComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;


private:
    // Menu bar
    juce::MenuBarComponent menuBar;

    // Toolbar area
    juce::Label projectNameLabel;
    juce::TextButton backButton;
    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton stopButton;
    juce::TextButton skipButton;
    juce::TextButton selectModeButton;
    juce::TextButton editModeButton;
    juce::TextButton pianoRollButton;
    juce::TextButton tempoButton;
    juce::TextButton timeSigButton;
    juce::Label lyricsLabel;
    juce::TextEditor lyricsInput;
    juce::Label logoLabel;

	PianoRollComponent pianoRoll;
	bool pianoRollVisible = false;

    juce::String currentProjectName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWComponent)
};