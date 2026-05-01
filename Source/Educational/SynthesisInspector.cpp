#include "SynthesisInspector.h"
#include "../Audio/VocalSynthEngine.h"

SynthesisInspector::SynthesisInspector()
{
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(patternNameLabel);
    addAndMakeVisible(wordCounterLabel);
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    addAndMakeVisible(inputLabel);
    addAndMakeVisible(phonemeLabel);
    addAndMakeVisible(explanationBox);

    titleLabel.setText("Synthesis Inspector", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);

    patternNameLabel.setFont(juce::Font(13.0f, juce::Font::italic));
    patternNameLabel.setColour(juce::Label::textColourId, juce::Colour(180, 140, 210));

    wordCounterLabel.setFont(juce::Font(12.0f));
    wordCounterLabel.setColour(juce::Label::textColourId, juce::Colour(150, 150, 180));
    wordCounterLabel.setJustificationType(juce::Justification::centred);

    prevButton.setButtonText("<");
    nextButton.setButtonText(">");
    for (auto* b : { &prevButton, &nextButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }

    prevButton.onClick = [this]()
        {
            if (patternWords.isEmpty()) return;
            currentWordIndex = (currentWordIndex - 1 + patternWords.size()) % patternWords.size();
            showCurrentWord();
        };

    nextButton.onClick = [this]()
        {
            if (patternWords.isEmpty()) return;
            currentWordIndex = (currentWordIndex + 1) % patternWords.size();
            showCurrentWord();
        };

    explanationBox.setMultiLine(true);
    explanationBox.setReadOnly(true);
    explanationBox.setScrollbarsShown(true);
    explanationBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0d1f));
    explanationBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    explanationBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::cyan.withAlpha(0.3f));

    setVisible(EducationalModeManager::getInstance().isEnabled());
    EducationalModeManager::getInstance().addListener(this);

    clearPatternData();
}

SynthesisInspector::~SynthesisInspector()
{
    EducationalModeManager::getInstance().removeListener(this);
}

void SynthesisInspector::setPatternData(const juce::String& patternName,
    const juce::StringArray& words,
    const juce::StringArray& pitchNames)
{
    currentPatternName = patternName;
    patternWords = words;
    patternPitches = pitchNames;
    currentWordIndex = 0;

    patternNameLabel.setText("Pattern: " + patternName, juce::dontSendNotification);

    if (patternWords.isEmpty())
    {
        inputLabel.setText("(no lyrics in this pattern)", juce::dontSendNotification);
        phonemeLabel.setText("", juce::dontSendNotification);
        explanationBox.setText("This pattern has no lyrics yet. Double-click the pattern "
            "in the browser to open the piano roll and type a word "
            "into a note.", juce::dontSendNotification);
        wordCounterLabel.setText("0 / 0", juce::dontSendNotification);
        prevButton.setEnabled(false);
        nextButton.setEnabled(false);
        return;
    }

    prevButton.setEnabled(patternWords.size() > 1);
    nextButton.setEnabled(patternWords.size() > 1);
    showCurrentWord();
}

void SynthesisInspector::setPatternName(const juce::String& patternName)
{
    currentPatternName = patternName;
    patternNameLabel.setText("Pattern: " + patternName, juce::dontSendNotification);
}

void SynthesisInspector::clearPatternData()
{
    currentPatternName = "";
    patternWords.clear();
    patternPitches.clear();
    currentWordIndex = 0;

    patternNameLabel.setText("(no pattern selected)", juce::dontSendNotification);
    inputLabel.setText("Click the Inspector button and pick a pattern to begin.",
        juce::dontSendNotification);
    phonemeLabel.setText("", juce::dontSendNotification);
    explanationBox.setText("", juce::dontSendNotification);
    wordCounterLabel.setText("0 / 0", juce::dontSendNotification);
    prevButton.setEnabled(false);
    nextButton.setEnabled(false);
}

void SynthesisInspector::showCurrentWord()
{
    if (patternWords.isEmpty()) return;

    if (currentWordIndex < 0) currentWordIndex = 0;
    if (currentWordIndex >= patternWords.size()) currentWordIndex = patternWords.size() - 1;

    juce::String word = patternWords[currentWordIndex];
    juce::String pitch = (currentWordIndex < patternPitches.size())
        ? patternPitches[currentWordIndex] : juce::String("?");

    wordCounterLabel.setText(juce::String(currentWordIndex + 1) + " / "
        + juce::String(patternWords.size()), juce::dontSendNotification);

    juce::StringArray phonemes;
    if (synthEngine != nullptr)
        phonemes = synthEngine->lookupPhonemes(word);

    onPhonemeResolved(word, phonemes, pitch);
}

void SynthesisInspector::onPhonemeResolved(const juce::String& inputWord,
    const juce::StringArray& phonemes,
    const juce::String& pitchNote)
{
    inputLabel.setText(
        "Input: \"" + inputWord + "\"   |   Pitch: " + pitchNote,
        juce::dontSendNotification);

    juce::String phonemeStr = "Phonemes: ";
    if (phonemes.isEmpty())
    {
        phonemeStr += "(not found in dictionary)";
    }
    else
    {
        for (auto& p : phonemes)
            phonemeStr += "[" + p + "] ";
    }
    phonemeLabel.setText(phonemeStr, juce::dontSendNotification);

    juce::String explanation;
    explanation += "What is happening:\n\n";
    explanation += "1. You typed \"" + inputWord + "\" as the lyric for this note.\n\n";

    if (phonemes.isEmpty())
    {
        explanation += "2. The word was not found in the CMU pronouncing dictionary, "
            "so VocalNite falls back to letter-by-letter synthesis. "
            "Custom or made-up words may produce unexpected results.\n\n";
    }
    else
    {
        explanation += "2. VocalNite broke it into "
            + juce::String(phonemes.size())
            + " phoneme" + (phonemes.size() == 1 ? "" : "s")
            + " using the ARPAbet system.\n\n";
    }

    explanation += "3. Each phoneme is matched to a .wav sample in the voice bank "
        "(the pitch folder closest to " + pitchNote + ").\n\n";
    explanation += "4. Adjacent phonemes are joined as diphones (e.g. \"HH-AH\") when "
        "available, or as singles with a short crossfade otherwise.\n\n";
    explanation += "5. The final audio is sent to JUCE's audio device manager for "
        "playback, timed to the project BPM.";

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

    // Top row: title on the left, word navigator on the right
    auto topRow = area.removeFromTop(30);
    auto navArea = topRow.removeFromRight(150);
    titleLabel.setBounds(topRow);

    // Word navigator: [<]  3 / 8  [>]
    int btnW = 28;
    prevButton.setBounds(navArea.removeFromLeft(btnW));
    nextButton.setBounds(navArea.removeFromRight(btnW));
    wordCounterLabel.setBounds(navArea);

    area.removeFromTop(4);
    patternNameLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(6);
    inputLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);
    phonemeLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    explanationBox.setBounds(area);
}

void SynthesisInspector::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    // Header bar
    g.setColour(juce::Colour(0xff0d0d1f));
    g.fillRect(0, 0, getWidth(), 34);

    // Cyan border
    g.setColour(juce::Colours::cyan.withAlpha(0.4f));
    g.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(1.0f),
        6.0f, 1.5f);
}