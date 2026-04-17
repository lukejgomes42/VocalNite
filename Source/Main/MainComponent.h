#pragma once
#include <JuceHeader.h>
#include "../Animation/StarSystem.h"
#include "../Animation/GlowLines.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent : public juce::Component,
    private juce::Timer
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    std::function<void(const juce::String& username)> onAuthenticationSuccess;

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
    juce::TextButton verifyButton{ "Verify Account" };

    void showAuthDialog(const juce::String& type);
    void showVerifyDialog();

    // Animation
    GlowLines glowLines{ 5 };
    StarSystem starSystem{ 50 };

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
