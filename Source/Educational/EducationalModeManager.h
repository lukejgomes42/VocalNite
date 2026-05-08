#pragma once
#include <JuceHeader.h>

// =============================================================================
//  EducationalModeManager
//  Singleton that holds the educational-mode on/off flag and broadcasts
//  changes to registered Listeners. Only accessible to verified educational
//  accounts — gating is enforced in ProjectManagerComponent::applyUserType().
// =============================================================================
class EducationalModeManager
{
public:
    static EducationalModeManager& getInstance()
    {
        static EducationalModeManager instance;
        return instance;
    }

    struct Listener
    {
        virtual void educationalModeChanged(bool enabled) = 0;
        virtual ~Listener() = default;
    };

    bool isEnabled() const { return enabled; }

    void setEnabled(bool state)
    {
        enabled = state;
        listeners.call([state](Listener& l) { l.educationalModeChanged(state); });
    }

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

private:
    EducationalModeManager() = default;

    EducationalModeManager(const EducationalModeManager&) = delete;
    EducationalModeManager& operator=(const EducationalModeManager&) = delete;
    EducationalModeManager(EducationalModeManager&&) = delete;
    EducationalModeManager& operator=(EducationalModeManager&&) = delete;

    bool enabled = false;
    juce::ListenerList<Listener> listeners;
};