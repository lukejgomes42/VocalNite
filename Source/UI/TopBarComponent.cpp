#include "TopBarComponent.h"

TopBarComponent::TopBarComponent()
{
    // App title
    appTitle.setFont(juce::Font(24.0f, juce::Font::bold));
    appTitle.setJustificationType(juce::Justification::centredLeft);
    appTitle.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(appTitle);

    // Username label
    userLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    userLabel.setJustificationType(juce::Justification::centredRight);
    userLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(userLabel);

    // Logout button
    logoutButton.setButtonText("Logout");
    logoutButton.setColour(juce::TextButton::buttonColourId, juce::Colour(200, 30, 30));
    logoutButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    logoutButton.onClick = [this]()
        {
            if (onLogoutClicked)
                onLogoutClicked();
        };
    addAndMakeVisible(logoutButton);
}

void TopBarComponent::setUsername(const juce::String& name)
{
    userLabel.setText(name, juce::dontSendNotification);
}

void TopBarComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black); // simple black top bar
    g.setColour(juce::Colour(80, 80, 80));
    g.drawRect(getLocalBounds(), 1); // subtle border
}

void TopBarComponent::resized()
{
    auto area = getLocalBounds().reduced(10, 5);

    logoutButton.setBounds(area.removeFromRight(100));
    userLabel.setBounds(area.removeFromRight(150));
    appTitle.setBounds(area);
}