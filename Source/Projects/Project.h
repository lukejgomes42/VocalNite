#pragma once
#include <JuceHeader.h>

// =============================================================================
//  Project
//  Thin wrapper for the per-project .vnite file and its DB record.
//  The vnite file is a small JSON blob (name, version, project_id) that
//  sits alongside the project folder so the ProjectManager can re-open it
//  without a DB query.
// =============================================================================
class Project
{
public:
    Project() = default;
    ~Project() = default;

    bool createNew(const juce::File& folder, const juce::String& name, int userId);
    bool load(const juce::File& projectFile);
    bool save();

    juce::String getName()       const { return name; }
    juce::File   getProjectFile() const { return projectFile; }
    int          getProjectId()   const { return projectId; }

private:
    juce::String name;
    juce::File   projectFile;
    int          projectId = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Project)
};