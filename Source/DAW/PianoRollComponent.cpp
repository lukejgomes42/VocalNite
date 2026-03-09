#include <JuceHeader.h>
#include "PianoRollComponent.h"

PianoRollComponent::PianoRollComponent()
{
    addAndMakeVisible(verticalScroll);
    addAndMakeVisible(horizontalScroll);

    verticalScroll.addListener(this);
    horizontalScroll.addListener(this);

    verticalScroll.setRangeLimits(0.0, numKeys * noteHeight);
    horizontalScroll.setRangeLimits(0.0, numBeats * cellWidth);

    verticalScroll.setCurrentRange(0.0, 200.0);
    horizontalScroll.setCurrentRange(0.0, 400.0);

    setSize(900, 400);
}

PianoRollComponent::~PianoRollComponent() {}

bool PianoRollComponent::isBlackKey(int noteIndex) const
{
    int note = noteIndex % 12;
    return (note == 1 || note == 3 || note == 6 || note == 8 || note == 10);
}

juce::Colour PianoRollComponent::getNoteColour(int pitch) const
{
    return isBlackKey(pitch) ? juce::Colour(180, 80, 220) : juce::Colour(120, 60, 200);
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &verticalScroll)
        verticalOffset = newRangeStart;
    else if (bar == &horizontalScroll)
        horizontalOffset = newRangeStart;
    repaint();
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    int gridX = e.x - keyWidth + (int)horizontalOffset;
    int gridY = e.y - headerHeight + (int)verticalOffset;

    if (e.x < keyWidth || e.y < headerHeight)
        return;

    int beat = gridX / cellWidth;
    int pitch = gridY / noteHeight;

    if (beat < 0 || beat >= numBeats || pitch < 0 || pitch >= numKeys)
        return;

    // Toggle note on/off
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        if (placedNotes[i].pitch == pitch && placedNotes[i].beat == beat)
        {
            placedNotes.remove(i);
            repaint();
            return;
        }
    }

    placedNotes.add({ pitch, beat });
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& e) {}
void PianoRollComponent::mouseDrag(const juce::MouseEvent& e) {}

void PianoRollComponent::paint(juce::Graphics& g)
{
    int scrolledY = (int)verticalOffset;
    int scrolledX = (int)horizontalOffset;
    int gridWidth = getWidth() - keyWidth - 12;
    int gridHeight = getHeight() - headerHeight - 12;

    // Background
    g.fillAll(juce::Colour(15, 15, 25));

    // ── Beat header ──
    g.setColour(juce::Colour(30, 30, 50));
    g.fillRect(keyWidth, 0, gridWidth, headerHeight);
    g.setColour(juce::Colours::grey);
    g.setFont(11.0f);
    int startBeat = scrolledX / cellWidth;
    for (int b = startBeat; b < numBeats; ++b)
    {
        int x = keyWidth + b * cellWidth - scrolledX;
        if (x > getWidth()) break;
        g.drawText(juce::String(b + 1), x + 2, 0, cellWidth, headerHeight, juce::Justification::centredLeft);
        g.setColour(juce::Colour(50, 50, 80));
        g.drawLine(x, 0, x, headerHeight, 1.0f);
        g.setColour(juce::Colours::grey);
    }

    // ── Piano keys ──
    int startKey = scrolledY / noteHeight;
    for (int i = startKey; i < numKeys; ++i)
    {
        int y = headerHeight + i * noteHeight - scrolledY;
        if (y > getHeight()) break;

        bool black = isBlackKey(i);

        // White key background
        g.setColour(black ? juce::Colours::black : juce::Colours::white);
        g.fillRect(0, y, keyWidth, noteHeight);

        // Key border
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(0, y, keyWidth, noteHeight, 1);

        // Note name on white keys
        if (!black)
        {
            int octave = (numKeys - 1 - i) / 12;
            int noteInOctave = (numKeys - 1 - i) % 12;
            juce::String noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            if (noteInOctave == 0)
            {
                g.setColour(juce::Colours::black);
                g.setFont(10.0f);
                g.drawText("C" + juce::String(octave), 2, y, keyWidth - 4, noteHeight, juce::Justification::centredLeft);
            }
        }
    }

    // ── Grid ──
    for (int i = startKey; i < numKeys; ++i)
    {
        int y = headerHeight + i * noteHeight - scrolledY;
        if (y > getHeight()) break;

        bool black = isBlackKey(i);
        g.setColour(black ? juce::Colour(20, 20, 35) : juce::Colour(28, 28, 45));
        g.fillRect(keyWidth, y, gridWidth, noteHeight);

        g.setColour(juce::Colour(45, 45, 70));
        g.drawLine(keyWidth, y, keyWidth + gridWidth, y, 1.0f);
    }

    // Vertical beat lines on grid
    for (int b = startBeat; b <= numBeats; ++b)
    {
        int x = keyWidth + b * cellWidth - scrolledX;
        if (x > getWidth()) break;
        g.setColour(b % 4 == 0 ? juce::Colour(70, 70, 100) : juce::Colour(40, 40, 65));
        g.drawLine(x, headerHeight, x, getHeight() - 12, 1.0f);
    }

    // ── Placed notes ──
    for (auto& note : placedNotes)
    {
        int x = keyWidth + note.beat * cellWidth - scrolledX;
        int y = headerHeight + note.pitch * noteHeight - scrolledY;
        if (x + cellWidth < keyWidth || x > getWidth()) continue;
        if (y + noteHeight < headerHeight || y > getHeight()) continue;

        g.setColour(getNoteColour(note.pitch));
        g.fillRoundedRectangle(x + 1, y + 1, cellWidth - 2, noteHeight - 2, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawRoundedRectangle(x + 1, y + 1, cellWidth - 2, noteHeight - 2, 3.0f, 1.0f);
    }

    // Clip boundary
    g.setColour(juce::Colour(15, 15, 25));
    g.fillRect(0, 0, keyWidth, headerHeight);
}

void PianoRollComponent::resized()
{
    int scrollBarThickness = 12;
    verticalScroll.setBounds(getWidth() - scrollBarThickness, headerHeight,
        scrollBarThickness, getHeight() - headerHeight - scrollBarThickness);
    horizontalScroll.setBounds(keyWidth, getHeight() - scrollBarThickness,
        getWidth() - keyWidth - scrollBarThickness, scrollBarThickness);

    verticalScroll.setCurrentRange(verticalOffset, getHeight() - headerHeight);
    horizontalScroll.setCurrentRange(horizontalOffset, getWidth() - keyWidth);
}