#pragma once
#include <JuceHeader.h>
#include "EducationalModeManager.h"

// =============================================================================
//  HighlightOverlay
//  Draws a pulsing cyan ring around a target component. Placed as a sibling
//  inside the parent and sized to cover the target + a small inset.
//  No-ops if Educational Mode is off or the target is null.
// =============================================================================
class HighlightOverlay : public juce::Component,
    private juce::Timer
{
public:
    HighlightOverlay()
    {
        setInterceptsMouseClicks(false, false);
        setVisible(false);
    }

    // Pulse-highlight any component once (fades over ~1 second at 25 fps).
    void highlight(juce::Component* target,
        juce::Colour colour = juce::Colours::cyan)
    {
        if (!EducationalModeManager::getInstance().isEnabled() || target == nullptr)
            return;

        highlightColour = colour;
        alpha = 1.0f;
        setVisible(true);

        if (auto* parent = target->getParentComponent())
            setBounds(parent->getLocalArea(target, target->getLocalBounds()).expanded(5));

        startTimer(40);   // ~25 fps fade
    }

    void paint(juce::Graphics& g) override
    {
        // Outer ring
        g.setColour(highlightColour.withAlpha(alpha * 0.7f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 8.0f, 3.0f);

        // Inner glow fill
        g.setColour(highlightColour.withAlpha(alpha * 0.15f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 8.0f);
    }

private:
    void timerCallback() override
    {
        alpha -= 0.04f;
        if (alpha <= 0.0f)
        {
            alpha = 0.0f;
            setVisible(false);
            stopTimer();
        }
        repaint();
    }

    juce::Colour highlightColour = juce::Colours::cyan;
    float        alpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HighlightOverlay)
};