#include "StarSystem.h"

StarSystem::StarSystem(int numStars)
{
    stars.ensureStorageAllocated(numStars);

    for (int i = 0; i < numStars; ++i)
    {
        Star s;
        s.x = juce::Random::getSystemRandom().nextFloat();
        s.y = juce::Random::getSystemRandom().nextFloat();
        s.size = 2.0f + juce::Random::getSystemRandom().nextFloat() * 4.0f;
        s.alpha = 0.7f + juce::Random::getSystemRandom().nextFloat() * 0.3f;

        stars.add(s);
    }
}

void StarSystem::setBounds(int width, int height)
{
    areaWidth = width;
    areaHeight = height;

    for (auto& s : stars)
    {
        s.x *= areaWidth;
        s.y *= areaHeight;
    }
}

void StarSystem::update(float glowPhase, int height)
{
    for (auto& s : stars)
    {
        s.y += 0.3f;

        if (s.y > height)
            s.y = 0.0f;

        s.alpha = 0.5f + 0.5f * std::sin(glowPhase * 0.01f + s.x + s.y);
    }
}

void StarSystem::draw(juce::Graphics& g)
{
    for (auto& s : stars)
    {
        g.setColour(juce::Colours::white.withAlpha(s.alpha));
        g.fillEllipse(s.x, s.y, s.size, s.size);
    }
}
