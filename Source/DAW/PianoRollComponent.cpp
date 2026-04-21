#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "../Database/DatabaseManager.h"
#include "../Educational/EducationalModeManager.h"
#include "../Educational/TooltipRegistry.h"
#include <libpq-fe.h>

PianoRollComponent::PianoRollComponent(int patternId)
    : currentPatternId(patternId)
{
    addAndMakeVisible(verticalScroll);
    addAndMakeVisible(horizontalScroll);

    verticalScroll.addListener(this);
    horizontalScroll.addListener(this);

    verticalScroll.setRangeLimits(0.0, numKeys * noteHeight);
    horizontalScroll.setRangeLimits(0.0, numBeats * cellWidth);

    verticalScroll.setCurrentRange(0.0, 200.0);
    horizontalScroll.setCurrentRange(0.0, 400.0);

    // Lyric editor setup
    lyricEditor.setMultiLine(false);
    lyricEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(40, 10, 60));
    lyricEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    lyricEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::hotpink);
    lyricEditor.setVisible(false);
    lyricEditor.onReturnKey = [this]()
        {
            if (editingNoteIndex >= 0 && editingNoteIndex < placedNotes.size())
            {
                juce::String lyric = lyricEditor.getText();
                placedNotes.getReference(editingNoteIndex).lyric = lyric;

                // Update in database
                if (currentPatternId >= 0)
                {
                    std::string patternIdStr = std::to_string(currentPatternId);
                    std::string pitchStr = std::to_string(placedNotes[editingNoteIndex].pitch);
                    std::string beatStr = std::to_string(placedNotes[editingNoteIndex].beat);
                    const char* params[4] = { lyric.toRawUTF8(), patternIdStr.c_str(), pitchStr.c_str(), beatStr.c_str() };

                    PGresult* res = PQexecParams(DatabaseManager::get().db(),
                        "UPDATE PatternNotes SET lyric = $1 WHERE pattern_id = $2 AND pitch = $3 AND beat = $4",
                        4, nullptr, params, nullptr, nullptr, 0);

                    if (PQresultStatus(res) != PGRES_COMMAND_OK)
                        DBG("Lyric save error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
                    PQclear(res);
                }

                editingNoteIndex = -1;
                lyricEditor.setVisible(false);
                repaint();
            }
        };
    lyricEditor.onEscapeKey = [this]()
        {
            editingNoteIndex = -1;
            lyricEditor.setVisible(false);
        };
    addAndMakeVisible(lyricEditor);

    loadNotes();

    // Educational-mode tooltips
    EducationalModeManager::getInstance().addListener(this);
    applyTooltips(EducationalModeManager::getInstance().isEnabled());

    setSize(900, 400);
}

PianoRollComponent::~PianoRollComponent()
{
    EducationalModeManager::getInstance().removeListener(this);
    if (onEditorClosed)
        onEditorClosed();
}

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
    if (e.mods.isRightButtonDown())
    {
        int gridX = e.x - keyWidth + (int)horizontalOffset;
        int gridY = e.y - headerHeight + (int)verticalOffset;

        if (e.x >= keyWidth && e.y >= headerHeight)
        {
            int beat = gridX / cellWidth;
            int pitch = gridY / noteHeight;

            for (int i = placedNotes.size() - 1; i >= 0; --i)
            {
                if (placedNotes[i].pitch == pitch &&
                    beat >= placedNotes[i].beat &&
                    beat < placedNotes[i].beat + placedNotes[i].duration)
                {
                    deleteNote(placedNotes[i].pitch, placedNotes[i].beat);
                    placedNotes.remove(i);
                    repaint();
                    return;
                }
            }
        }
        return;
    }
    int gridX = e.x - keyWidth + (int)horizontalOffset;
    int gridY = e.y - headerHeight + (int)verticalOffset;

    if (e.x < keyWidth || e.y < headerHeight)
        return;

    int beat = gridX / cellWidth;
    int pitch = gridY / noteHeight;

    if (beat < 0 || beat >= numBeats || pitch < 0 || pitch >= numKeys)
        return;

    // Check for note resize
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        auto& note = placedNotes.getReference(i);
        int noteX = keyWidth + note.beat * cellWidth - (int)horizontalOffset;
        int noteY = headerHeight + note.pitch * noteHeight - (int)verticalOffset;
        int noteW = cellWidth * note.duration;

        juce::Rectangle<int> resizeZone(noteX + noteW - 6, noteY, 6, noteHeight);
        if (resizeZone.contains(e.x, e.y))
        {
            isResizingNote = true;
            resizingNoteIndex = i;
            return;
        }
    }
    // Toggle note on/off
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        if (placedNotes[i].pitch == pitch && placedNotes[i].beat == beat)
        {
            // If clicking existing note, open lyric editor
            editingNoteIndex = i;
            int noteX = keyWidth + beat * cellWidth - (int)horizontalOffset;
            int noteY = headerHeight + pitch * noteHeight - (int)verticalOffset;
            lyricEditor.setText(placedNotes[i].lyric);
            lyricEditor.setBounds(noteX, noteY, cellWidth, noteHeight);
            lyricEditor.setVisible(true);
            lyricEditor.grabKeyboardFocus();
            lyricEditor.selectAll();
            return;
        }
    }

    saveNote(pitch, beat, "");
    Note newNote;
    newNote.pitch = pitch;
    newNote.beat = beat;
    newNote.lyric = "";
    placedNotes.add(newNote);

    // Show lyric editor for new note
    editingNoteIndex = placedNotes.size() - 1;
    int noteX = keyWidth + beat * cellWidth - (int)horizontalOffset;
    int noteY = headerHeight + pitch * noteHeight - (int)verticalOffset;
    lyricEditor.setText("");
    lyricEditor.setBounds(noteX, noteY, cellWidth, noteHeight);
    lyricEditor.setVisible(true);
    lyricEditor.grabKeyboardFocus();
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& e)
{
    if (isResizingNote && resizingNoteIndex >= 0)
    {
        auto& note = placedNotes[resizingNoteIndex];
        std::string patternIdStr = std::to_string(currentPatternId);
        std::string pitchStr = std::to_string(note.pitch);
        std::string beatStr = std::to_string(note.beat);
        std::string durationStr = std::to_string(note.duration);
        const char* params[4] = { durationStr.c_str(), patternIdStr.c_str(), pitchStr.c_str(), beatStr.c_str() };

        PGresult* res = PQexecParams(DatabaseManager::get().db(),
            "UPDATE PatternNotes SET duration = $1 WHERE pattern_id = $2 AND pitch = $3 AND beat = $4",
            4, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            DBG("Note resize error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
        PQclear(res);

        isResizingNote = false;
        resizingNoteIndex = -1;
        repaint();
    }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (isResizingNote && resizingNoteIndex >= 0)
    {
        int gridX = e.x - keyWidth + (int)horizontalOffset;
        int newEnd = gridX / cellWidth + 1;
        int startBeat = placedNotes[resizingNoteIndex].beat;
        int newDuration = std::max(1, newEnd - startBeat);
        placedNotes.getReference(resizingNoteIndex).duration = newDuration;
        repaint();
    }
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.x < keyWidth)
    {
        // Scrolling over piano keys - scroll vertically
        double newOffset = verticalOffset - (double)wheel.deltaY * 60.0;
        newOffset = std::max(0.0, std::min(newOffset, verticalScroll.getRangeLimit().getEnd()));
        verticalScroll.setCurrentRangeStart(newOffset);
    }
    else
    {
        // Scrolling over grid - vertical scrolls tracks, horizontal scrolls beats
        double newVOffset = verticalOffset - (double)wheel.deltaY * 60.0;
        newVOffset = std::max(0.0, std::min(newVOffset, verticalScroll.getRangeLimit().getEnd()));
        verticalScroll.setCurrentRangeStart(newVOffset);

        double newHOffset = horizontalOffset - (double)wheel.deltaX * 60.0;
        newHOffset = std::max(0.0, std::min(newHOffset, horizontalScroll.getRangeLimit().getEnd()));
        horizontalScroll.setCurrentRangeStart(newHOffset);
    }
}
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

        int noteW = cellWidth * note.duration;
        g.setColour(getNoteColour(note.pitch));
        g.fillRoundedRectangle(x + 1, y + 1, noteW - 2, noteHeight - 2, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawRoundedRectangle(x + 1, y + 1, noteW - 2, noteHeight - 2, 3.0f, 1.0f);
        if (note.lyric.isNotEmpty())
        {
            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText(note.lyric, x + 2, y + 1, noteW - 4, noteHeight - 2, juce::Justification::centred);
        }
        // Resize handle on right edge
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillRect(x + noteW - 4, y + 2, 3, noteHeight - 4);
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

void PianoRollComponent::saveNote(int pitch, int beat, const juce::String& lyric, int duration)
{
    if (currentPatternId < 0) return;

    std::string patternIdStr = std::to_string(currentPatternId);
    std::string pitchStr = std::to_string(pitch);
    std::string beatStr = std::to_string(beat);
    std::string durationStr = std::to_string(duration);
    const char* params[5] = { patternIdStr.c_str(), pitchStr.c_str(), beatStr.c_str(), lyric.toRawUTF8(), durationStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "INSERT INTO PatternNotes (pattern_id, pitch, beat, lyric, duration) VALUES ($1, $2, $3, $4, $5)",
        5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        DBG("Save note error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    PQclear(res);
}

void PianoRollComponent::deleteNote(int pitch, int beat)
{
    if (currentPatternId < 0) return;

    std::string patternIdStr = std::to_string(currentPatternId);
    std::string pitchStr = std::to_string(pitch);
    std::string beatStr = std::to_string(beat);
    const char* params[3] = { patternIdStr.c_str(), pitchStr.c_str(), beatStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "DELETE FROM PatternNotes WHERE pattern_id = $1 AND pitch = $2 AND beat = $3",
        3, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        DBG("Delete note error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    PQclear(res);
}

void PianoRollComponent::loadNotes()
{
    if (currentPatternId < 0) return;
    placedNotes.clear();

    std::string patternIdStr = std::to_string(currentPatternId);
    const char* params[1] = { patternIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "SELECT pitch, beat, lyric, duration FROM PatternNotes WHERE pattern_id = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        int rows = PQntuples(res);
        for (int row = 0; row < rows; ++row)
        {
            Note n;
            n.pitch = std::stoi(PQgetvalue(res, row, 0));
            n.beat = std::stoi(PQgetvalue(res, row, 1));
            n.lyric = juce::String(PQgetvalue(res, row, 2));
            n.duration = std::stoi(PQgetvalue(res, row, 3));
            placedNotes.add(n);
        }
        DBG("Loaded " + juce::String(placedNotes.size()) + " notes");
    }
    else
    {
        DBG("Load notes error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    }
    PQclear(res);
}
// ============================================================================
//  Educational mode: tooltips
// ============================================================================

void PianoRollComponent::educationalModeChanged(bool isEnabled)
{
    applyTooltips(isEnabled);
}

void PianoRollComponent::applyTooltips(bool eduEnabled)
{
    if (eduEnabled)
    {
        // The whole roll explains tile placement + row/column semantics
        setTooltip(TooltipRegistry::get("pianoRollTile"));
        // The inline text editor explains lyric → phoneme conversion
        lyricEditor.setTooltip(TooltipRegistry::get("lyricInput"));
    }
    else
    {
        setTooltip({});
        lyricEditor.setTooltip({});
    }
}