#pragma once
#include <JuceHeader.h>

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent : public juce::Component,
    private juce::Timer,
    private juce::MouseListener
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    // Mouse hover callbacks (for buttons)
    void mouseEnter(const juce::MouseEvent& event);
    void mouseExit(const juce::MouseEvent& event);

private:
    juce::Label titleLabel;
    juce::TextButton signupButton{ "Sign Up" };
    juce::TextButton loginButton{ "Login" };

    void showAuthDialog(const juce::String& type);

    // Animation
    juce::Array<float> lineOffsets;
    float glowPhase = 0.0f;

    // Animation for stars
    struct Star
    {
        float x, y;
        float size;
        float alpha;
    };
    juce::Array<Star> stars;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
