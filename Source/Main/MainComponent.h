#pragma once
#include <JuceHeader.h>
#include "../Animation/StarSystem.h"
#include "../Animation/GlowLines.h"

// =============================================================================
//  MainComponent
//  Login screen. Hosts Sign Up, Login, and Verify Account buttons over an
//  animated star-field backdrop. Clicking the title area swaps the text for
//  the logo image with a sparkle-burst animation.
// =============================================================================
class MainComponent : public juce::Component,
    private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // Fired on successful login/signup with the authenticated username.
    std::function<void(const juce::String& username)> onAuthenticationSuccess;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    juce::Label      titleLabel;
    juce::TextButton signupButton{ "Sign Up" };
    juce::TextButton loginButton{ "Login" };
    juce::TextButton verifyButton{ "Verify Account" };

    void showAuthDialog(const juce::String& type);
    void showVerifyDialog();

    // Optional logo image (Resources/logo.png). Falls back to gradient text.
    juce::Image          logoImage;
    bool                 showLogo = false;
    juce::Rectangle<int> titleHitArea;
    void                 loadLogoImage();

    // Sparkle / flash animation triggered when the logo swaps in
    struct Sparkle
    {
        float x = 0.0f, y = 0.0f;
        float vx = 0.0f, vy = 0.0f;
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 3.0f;
        juce::Colour colour;
    };
    juce::Array<Sparkle> sparkles;
    float        logoAlpha = 0.0f;
    bool         flashActive = false;
    juce::uint32 flashStartTimeMs = 0;

    void triggerSwapAnimation();
    void spawnSparkleBurst(int count);
    void drawSparkle(juce::Graphics& g, const Sparkle& s) const;

    // Background animation
    GlowLines  glowLines{ 5 };
    StarSystem starSystem{ 50 };

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};