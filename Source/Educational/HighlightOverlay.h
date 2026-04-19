// Source/Educational/HighlightOverlay.h
#pragma once
#include <JuceHeader.h>
#include "EducationalModeManager.h"

class HighlightOverlay : public juce::Component,
    private juce::Timer {
public:
    HighlightOverlay() {
        setInterceptsMouseClicks(false, false);
        setVisible(false);
    }

    // Call this to pulse-highlight any component
    void highlight(juce::Component* target,
        juce::Colour colour = juce::Colours::cyan) {
        if (!EducationalModeManager::getInstance().isEnabled()
            || target == nullptr) return;

        highlightColour = colour;
        alpha = 1.0f;
        setVisible(true);

        // Position overlay to match target in parent coordinates
        if (auto* parent = target->getParentComponent()) {
            setBounds(parent->getLocalArea(
                target,
                target->getLocalBounds()).expanded(5));
        }

        startTimer(40); // ~25fps fade
    }

    void paint(juce::Graphics& g) override {
        g.setColour(highlightColour.withAlpha(alpha * 0.7f));
        g.drawRoundedRectangle(
            getLocalBounds().toFloat().reduced(2.0f),
            8.0f, 3.0f);

        // Inner glow
        g.setColour(highlightColour.withAlpha(alpha * 0.15f));
        g.fillRoundedRectangle(
            getLocalBounds().toFloat().reduced(2.0f), 8.0f);
    }

private:
    void timerCallback() override {
        alpha -= 0.04f;
        if (alpha <= 0.0f) {
            alpha = 0.0f;
            setVisible(false);
            stopTimer();
        }
        repaint();
    }

    juce::Colour highlightColour = juce::Colours::cyan;
    float alpha = 0.0f;
};