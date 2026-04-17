#pragma once
#include <JuceHeader.h>

class Project
{
public:
    Project() = default;

    bool createNew(const juce::File& folder, const juce::String& name, int userId);
    bool load(const juce::File& projectFile);
    bool save();

    juce::String getName() const { return name; }
    juce::File getProjectFile() const { return projectFile; }
    int getProjectId() const { return projectId; }

private:
    juce::String name;
    juce::File projectFile;
    int projectId = -1;
};