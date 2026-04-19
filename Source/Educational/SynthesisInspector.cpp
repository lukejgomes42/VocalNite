#include "SynthesisInspector.h"

SynthesisInspector::SynthesisInspector()
{
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(inputLabel);
    addAndMakeVisible(phonemeLabel);
    addAndMakeVisible(explanationBox);

    titleLabel.setText("Synthesis Inspector",
        juce::dontSendNotification);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId,
        juce::Colours::cyan);

    explanationBox.setMultiLine(true);
    explanationBox.setReadOnly(true);
    explanationBox.setScrollbarsShown(true);

    setVisible(EducationalModeManager::getInstance().isEnabled());
    EducationalModeManager::getInstance().addListener(this);
}

SynthesisInspector::~SynthesisInspector()
{
    EducationalModeManager::getInstance().removeListener(this);
}

void SynthesisInspector::onPhonemeResolved(
    const juce::String& inputWord,
    const juce::StringArray& phonemes,
    const juce::String& pitchNote)
{
    inputLabel.setText(
        "Input:  \"" + inputWord + "\"   |   Pitch: " + pitchNote,
        juce::dontSendNotification);

    juce::String phonemeStr = "Phonemes:  ";
    for (auto& p : phonemes)
        phonemeStr += "[" + p + "]  ";
    phonemeLabel.setText(phonemeStr, juce::dontSendNotification);

    juce::String explanation;
    explanation += "What is happening:\n\n";
    explanation += "1. You typed \"" + inputWord + "\".\n";
    explanation += "2. VocalNite broke it into "
        + juce::String(phonemes.size())
        + " phoneme(s) using the ARPAbet system.\n";
    explanation += "3. Each phoneme is matched to a .wav sample "
        "in the voice bank (A3 or C4 folder).\n";
    explanation += "4. The samples are pitch-shifted to "
        + pitchNote
        + " and stitched together by the synthesis engine.\n";
    explanation += "5. The final audio is sent to JUCE's audio "
        "device manager for playback.";

    explanationBox.setText(explanation, juce::dontSendNotification);
}

void SynthesisInspector::educationalModeChanged(bool isEnabled)
{
    setVisible(isEnabled);
    if (auto* parent = getParentComponent())
        parent->resized();
}

void SynthesisInspector::resized()
{
    auto area = getLocalBounds().reduced(12);

    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);  // small gap
    inputLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);  // small gap
    phonemeLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);  // bigger gap before text box
    explanationBox.setBounds(area);  // text box gets all remaining space
}

void SynthesisInspector::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    // Header bar
    g.setColour(juce::Colour(0xff0d0d1f));
    g.fillRect(0, 0, getWidth(), 28);

    // Cyan border
    g.setColour(juce::Colours::cyan.withAlpha(0.4f));
    g.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(1.0f),
        6.0f, 1.5f);
}