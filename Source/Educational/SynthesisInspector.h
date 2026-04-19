#pragma once
#include <JuceHeader.h>
#include "EducationalModeManager.h"

class SynthesisInspector : public juce::Component,
    public EducationalModeManager::Listener
{
public:
    SynthesisInspector();
    ~SynthesisInspector() override;

    void onPhonemeResolved(const juce::String& inputWord,
        const juce::StringArray& phonemes,
        const juce::String& pitchNote);

    void educationalModeChanged(bool isEnabled) override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::Label      titleLabel;
    juce::Label      inputLabel;
    juce::Label      phonemeLabel;
    juce::TextEditor explanationBox;
};