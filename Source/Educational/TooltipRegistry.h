// Source/Educational/TooltipRegistry.h
#pragma once
#include <JuceHeader.h>
#include <map>
#include <string>

class TooltipRegistry {
public:
    static juce::String get(const juce::String& componentID) {
        static std::map<juce::String, juce::String> tips = {
            // DAW Page
            { "addPattern",
              "ADD PATTERN: Creates a new vocal pattern block. "
              "Patterns hold the lyrics and notes that get converted "
              "into phonemes by the synthesis engine." },

            { "addTrack",
              "ADD TRACK: A track is a single vocal lane on the "
              "timeline. You can have multiple tracks for harmonies "
              "or layered vocals." },

            { "bpmControl",
              "BPM (Beats Per Minute): Controls how fast your song "
              "plays. 120 BPM is a standard pop tempo. Higher = faster." },

            { "timeSignature",
              "TIME SIGNATURE: The top number is beats per measure, "
              "the bottom is the note value of each beat. 4/4 is the "
              "most common in music." },

            { "playButton",
              "PLAY: Triggers the synthesis engine to stitch phonemes "
              "from your voice bank together and play back your vocal." },

            { "pauseButton",
              "PAUSE: Temporarily stops playback. Press play again "
              "to resume from the same position." },

            { "stopButton",
              "STOP: Stops playback and returns the cursor to the start." },

            { "snapToggle",
              "SNAP: When on, patterns snap to the nearest beat grid "
              "position, keeping everything in time." },

            // Piano Roll
            { "pianoRollTile",
              "PIANO ROLL TILE: Click to place a note. The row "
              "determines pitch, the column determines timing. "
              "Type a lyric into the tile to assign a word." },

            { "lyricInput",
              "LYRIC INPUT: Type a word or syllable here. VocalNite "
              "converts it into phonemes using the ARPAbet system and "
              "maps them to audio samples in the voice bank." },

            // Project Manager
            { "newProject",
              "NEW PROJECT: Starts a fresh workspace. Your BPM, "
              "tracks, patterns, and lyrics will all be saved "
              "to the database." },

            { "openProject",
              "OPEN PROJECT: Load a previously saved project. "
              "All your tracks, clips, and settings will be restored." },
        };

        auto it = tips.find(componentID);
        return it != tips.end() ? it->second : juce::String{};
    }
};