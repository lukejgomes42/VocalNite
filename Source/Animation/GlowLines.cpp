#include "GlowLines.h"

GlowLines::GlowLines(int numLines)
{
    lineOffsets.clear();
    for (int i = 0; i < numLines; ++i)
        lineOffsets.add((float)i);
}

void GlowLines::setBounds(int w, int h)
{
    width = w;
    height = h;

    float spacing = height / (float)lineOffsets.size();
    for (int i = 0; i < lineOffsets.size(); ++i)
        lineOffsets.set(i, i * spacing);
}

void GlowLines::update()
{
    glowPhase += 1.0f;
    if (glowPhase > height)
        glowPhase = 0.0f;
}

void GlowLines::draw(juce::Graphics& g)
{
    g.setColour(juce::Colours::purple.withMultipliedAlpha(0.2f));

    for (auto offset : lineOffsets)
    {
        float y = std::fmod(offset + glowPhase, (float)height);
        g.drawLine(0.0f, y, (float)width, y, 2.0f);
    }
}
