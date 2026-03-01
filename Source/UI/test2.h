#pragma once
#include <JuceHeader.h>

class ProjectManagerComponent : public juce::Component
{
public:
    ProjectManagerComponent();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton createButton{ "Create New Project" };
    juce::TextButton openButton{ "Open Existing Project" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerComponent)
};