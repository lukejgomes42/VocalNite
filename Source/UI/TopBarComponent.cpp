#include "TopBarComponent.h"

TopBarComponent::TopBarComponent()
{
    // App title
    appTitle.setText("VocalNite", juce::dontSendNotification);
    appTitle.setFont(juce::Font(20.0f, juce::Font::bold));
    appTitle.setJustificationType(juce::Justification::centredLeft);
    appTitle.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    addAndMakeVisible(appTitle);

    // Username label
    userLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    userLabel.setJustificationType(juce::Justification::centredRight);
    userLabel.setColour(juce::Label::textColourId, juce::Colour(180, 140, 210));
    addAndMakeVisible(userLabel);

    // Logout button
    logoutButton.setButtonText("Logout");
    logoutButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 20, 100));
    logoutButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(110, 30, 140));
    logoutButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
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
    // Match the dark purple theme
    g.fillAll(juce::Colour(20, 10, 35));

    // Bottom border accent line
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(0, getHeight() - 1, getWidth(), getHeight() - 1, 1.0f);
}

void TopBarComponent::resized()
{
    auto area = getLocalBounds().reduced(12, 8);

    logoutButton.setBounds(area.removeFromRight(80));
    area.removeFromRight(8);
    userLabel.setBounds(area.removeFromRight(120));
    appTitle.setBounds(area);
}