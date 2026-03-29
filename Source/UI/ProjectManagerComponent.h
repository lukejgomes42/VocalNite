#pragma once
#include <JuceHeader.h>
#include "TopBarComponent.h"
#include "../Projects/Project.h"

class ProjectManagerComponent : public juce::Component,
    public juce::ListBoxModel
{
public:
    ProjectManagerComponent();
    void paint(juce::Graphics&) override;
    void resized() override;

    void setUsername(const juce::String& name) { topBar.setUsername(name); }
    std::function<void()> onLogout;
    std::function<void(const juce::String& projectName, int projectId)> onOpenProject;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    Project currentProject;
    TopBarComponent topBar;

    juce::TextButton createButton;
    juce::TextButton openButton;
    juce::Label statusLabel;
    juce::ListBox recentProjectsList;
    juce::Array<juce::File> recentFiles;

    void refreshRecentProjects();
    juce::Rectangle<int> getCentreCardBounds() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerComponent)
};