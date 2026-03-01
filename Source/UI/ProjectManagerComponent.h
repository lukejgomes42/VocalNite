#pragma once
#include <JuceHeader.h>
#include "TopBarComponent.h"

class ProjectManagerComponent : public juce::Component
{
public:
    ProjectManagerComponent();
    void paint(juce::Graphics&) override;
    void resized() override;

    void setUsername(const juce::String& name) { topBar.setUsername(name); }
    std::function<void()> onLogout; // forwards to MainWindow

private:
    TopBarComponent topBar;

    juce::TextButton createButton{ "Create New Project" };
    juce::TextButton openButton{ "Open Existing Project" };

    // Placeholder project list
    juce::Label placeholderLabel{ "No recent projects" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerComponent)
};