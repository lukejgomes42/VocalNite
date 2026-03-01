#include "ProjectManagerComponent.h"

ProjectManagerComponent::ProjectManagerComponent()
{
    // Forward logout callback
    topBar.onLogoutClicked = [this]()
        {
            if (onLogout)
                onLogout();
        };
    addAndMakeVisible(topBar);

    addAndMakeVisible(createButton);
    addAndMakeVisible(openButton);

    placeholderLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    placeholderLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholderLabel);
}

void ProjectManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0, 0, 0)); // dark background
}

void ProjectManagerComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds(area.removeFromTop(50).reduced(0, 0));

    auto buttonArea = area.removeFromBottom(100).reduced(50, 10);
    createButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(10));
    openButton.setBounds(buttonArea.reduced(10));

    placeholderLabel.setBounds(area.reduced(50, 0));
}