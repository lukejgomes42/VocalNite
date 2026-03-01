#include "test2.h"

ProjectManagerComponent::ProjectManagerComponent()
{
    addAndMakeVisible(createButton);
    addAndMakeVisible(openButton);
}

void ProjectManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void ProjectManagerComponent::resized()
{
    auto area = getLocalBounds().reduced(100);

    createButton.setBounds(area.removeFromTop(50));
    area.removeFromTop(20);
    openButton.setBounds(area.removeFromTop(50));
}