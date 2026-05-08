#pragma once
#include <JuceHeader.h>
#include "EducationalModeManager.h"

class VocalSynthEngine;

// =============================================================================
//  SynthesisInspector
//  Floating docked panel (hosted inside SynthesisInspectorWindow) that shows
//  a step-by-step breakdown of how a lyric is converted to phonemes and
//  rendered. Accessible only in Educational Mode.
//
//  Usage:
//      inspector.setSynthEngine(&vocalSynth);
//      inspector.setPatternData("My Pattern", words, pitches);
// =============================================================================
class SynthesisInspector : public juce::Component,
    public EducationalModeManager::Listener
{
public:
    SynthesisInspector();
    ~SynthesisInspector() override;

    // Provide the engine so the inspector can resolve phonemes itself.
    // Caller retains ownership.
    void setSynthEngine(VocalSynthEngine* engine) { synthEngine = engine; }

    // Supply the unique word list for the pattern being inspected.
    // words and pitchNames are parallel arrays ordered by first-played beat.
    void setPatternData(const juce::String& patternName,
        const juce::StringArray& words,
        const juce::StringArray& pitchNames);

    void clearPatternData();

    // ── Component overrides ─────────────────────────────────────────────────
    void educationalModeChanged(bool isEnabled) override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // Populate UI labels from the word at currentWordIndex.
    void showCurrentWord();

    // Render the phoneme breakdown for a single word into the UI labels.
    void onPhonemeResolved(const juce::String& inputWord,
        const juce::StringArray& phonemes,
        const juce::String& pitchNote);

    // ── Data ────────────────────────────────────────────────────────────────
    juce::String      currentPatternName;
    juce::StringArray patternWords;
    juce::StringArray patternPitches;
    int               currentWordIndex = 0;

    VocalSynthEngine* synthEngine = nullptr;

    // ── UI ──────────────────────────────────────────────────────────────────
    juce::Label      titleLabel;
    juce::Label      patternNameLabel;
    juce::Label      wordCounterLabel;
    juce::TextButton prevButton;
    juce::TextButton nextButton;
    juce::Label      inputLabel;
    juce::Label      phonemeLabel;
    juce::TextEditor explanationBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisInspector)
};