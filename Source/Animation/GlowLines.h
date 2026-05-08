#pragma once
#include <JuceHeader.h>

class GlowLines
{
public:
    GlowLines(int numLines = 5);

    void setBounds(int width, int height);
    void update();
    void draw(juce::Graphics& g);

private:
    juce::Array<float> lineOffsets;
    float glowPhase = 0.0f;
    int width = 0;
    int height = 0;
};