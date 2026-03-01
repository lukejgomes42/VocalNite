#pragma once
#include <JuceHeader.h>

class TopBarComponent : public juce::Component
{
public:
    TopBarComponent();
    ~TopBarComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setUsername(const juce::String& name);

    std::function<void()> onLogoutClicked;

private:
    juce::Label appTitle;
    juce::Label userLabel;
    juce::TextButton logoutButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBarComponent)
};