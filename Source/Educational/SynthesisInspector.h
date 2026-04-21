#pragma once
#include <JuceHeader.h>
#include "EducationalModeManager.h"

class VocalSynthEngine;  // forward declaration

class SynthesisInspector : public juce::Component,
    public EducationalModeManager::Listener
{
public:
    SynthesisInspector();
    ~SynthesisInspector() override;

    // Provide the engine so the inspector can resolve phonemes itself.
    // Caller keeps ownership.
    void setSynthEngine(VocalSynthEngine* engine) { synthEngine = engine; }

    // ── Word list API (new) ─────────────────────────────────────────────────
    // Words + matching pitch names are arrays of equal size, ordered by the
    // beat at which they first appear in the pattern ("lenient first-played
    // order"). Duplicates already removed by caller.
    void setPatternData(const juce::String& patternName,
        const juce::StringArray& words,
        const juce::StringArray& pitchNames);

    void clearPatternData();

    // Legacy single-shot update (kept for back-compat; still usable if some
    // other code wanted to push a one-off breakdown).
    void onPhonemeResolved(const juce::String& inputWord,
        const juce::StringArray& phonemes,
        const juce::String& pitchNote);

    // ── Component overrides ─────────────────────────────────────────────────
    void educationalModeChanged(bool isEnabled) override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // Data for the current pattern being inspected
    juce::String         currentPatternName;
    juce::StringArray    patternWords;
    juce::StringArray    patternPitches;
    int                  currentWordIndex = 0;

    VocalSynthEngine* synthEngine = nullptr;

    // UI
    juce::Label          titleLabel;
    juce::Label          patternNameLabel;
    juce::Label          wordCounterLabel;      // "3 / 8"
    juce::TextButton     prevButton;
    juce::TextButton     nextButton;
    juce::Label          inputLabel;
    juce::Label          phonemeLabel;
    juce::TextEditor     explanationBox;

    void showCurrentWord();   // refreshes labels + explanation from currentWordIndex

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisInspector)
};