#pragma once
#include <JuceHeader.h>

class StarSystem
{
public:
    StarSystem(int numStars);

    void update(float glowPhase, int height);
    void draw(juce::Graphics& g);

    void setBounds(int width, int height);

private:
    struct Star
    {
        float x, y;
        float size;
        float alpha;
    };

    juce::Array<Star> stars;

    int areaWidth = 0;
    int areaHeight = 0;
};