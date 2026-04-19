// Source/Educational/EducationalModeManager.h

#pragma once
#include <JuceHeader.h>

class EducationalModeManager {
public:
    static EducationalModeManager& getInstance() {
        static EducationalModeManager instance;
        return instance;
    }

    struct Listener {
        virtual void educationalModeChanged(bool enabled) = 0;
        virtual ~Listener() = default;
    };

    bool isEnabled() const { return enabled; }

    void setEnabled(bool state) {
        enabled = state;
        listeners.call([state](Listener& l) {
            l.educationalModeChanged(state);
            });
    }

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

private:
    EducationalModeManager() = default;
    bool enabled = false;
    juce::ListenerList<Listener> listeners;
};