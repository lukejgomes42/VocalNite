#pragma once
#include <JuceHeader.h>
#include "TopBarComponent.h"
#include "../Projects/Project.h"

class ProjectManagerComponent : public juce::Component
{
public:
    ProjectManagerComponent();
    void paint(juce::Graphics&) override;
    void resized() override;

    void setUsername(const juce::String& name) { topBar.setUsername(name); }
    std::function<void()> onLogout; // forwards to MainWindow
    std::function<void(const juce::String& projectName)> onOpenProject;

private:
    Project currentProject;
    TopBarComponent topBar;

    juce::TextButton createButton{ "Create New Project" };
    juce::TextButton openButton{ "Open Existing Project" };

    // Placeholder project list
    juce::Label placeholderLabel{ "No recent projects" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerComponent)
};