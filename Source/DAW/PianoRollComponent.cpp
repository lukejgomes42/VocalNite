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

    // Range limits + viewport size are both driven from resized() now.
    verticalScroll.setRangeLimits(0.0, (double)(numKeys * noteHeight));
    horizontalScroll.setRangeLimits(0.0, (double)(numBeats * cellWidth));

    // Lyric editor setup
    lyricEditor.setMultiLine(false);
    lyricEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(40, 10, 60));
    lyricEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    lyricEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::hotpink);
    lyricEditor.setVisible(false);
    lyricEditor.onReturnKey = [this]() { commitCurrentLyricEdit(); };
    lyricEditor.onEscapeKey = [this]() { discardCurrentLyricEdit(); };
    // Note: deliberately NOT wiring onFocusLost. JUCE posts focus-loss messages
    // asynchronously, which races with our mouseDown fluid-switch path (commit
    // old note → immediately open editor on new note). An async onFocusLost
    // firing after step 2 would clobber the newly-opened editor. All real
    // commit paths are explicit: Enter, click-on-different-note in mouseDown.
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

    // Keep the lyric editor glued to its note as the grid scrolls.
    if (lyricEditor.isVisible())
        positionLyricEditorForEditingNote();

    repaint();
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    // ------------------------------------------------------------------
    // Lyric-editor click dismissal
    // ------------------------------------------------------------------
    // If the editor is open, clicks that reach this handler are by
    // definition OUTSIDE the TextEditor (JUCE routes in-editor clicks
    // straight to it). We block those clicks from creating/selecting
    // notes and decide between commit and discard based on target:
    //   • click on the same note we're editing  → ignore (do nothing)
    //   • click on a different existing note / resize handle → commit
    //     the current lyric, then fall through so the click is handled
    //     normally (fluid switch to the next note)
    //   • click on empty grid or off-grid → discard and consume click
    if (lyricEditor.isVisible() && editingNoteIndex >= 0)
    {
        const int idx = findNoteIndexAtMouse(e);

        if (idx >= 0 && idx == editingNoteIndex)
            return;

        if (idx >= 0)
            commitCurrentLyricEdit();   // fall through to normal handling
        else
        {
            discardCurrentLyricEdit();
            return;
        }
    }

    // ------------------------------------------------------------------
    // Right-click: delete note under cursor
    // ------------------------------------------------------------------
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

    // ------------------------------------------------------------------
    // Left-click: resize / edit existing / create new
    // ------------------------------------------------------------------
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

    // Click on any cell within an existing note's duration → open lyric editor
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        const auto& n = placedNotes[i];
        if (n.pitch == pitch && beat >= n.beat && beat < n.beat + n.duration)
        {
            editingNoteIndex = i;
            lyricEditor.setText(n.lyric);
            positionLyricEditorForEditingNote();
            lyricEditor.setVisible(true);
            lyricEditor.grabKeyboardFocus();
            lyricEditor.selectAll();
            return;
        }
    }

    // Empty cell: create note + open editor
    saveNote(pitch, beat, "");
    Note newNote;
    newNote.pitch = pitch;
    newNote.beat = beat;
    newNote.lyric = "";
    placedNotes.add(newNote);

    editingNoteIndex = placedNotes.size() - 1;
    lyricEditor.setText("");
    positionLyricEditorForEditingNote();
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

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e,
    const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e);

    // Wheel step in pixels. Tiny deltas (macOS smooth scroll) still get a
    // consistent feel per notch.
    constexpr double kStep = 60.0;
    const double dy = (double)wheel.deltaY * kStep;
    const double dx = (double)wheel.deltaX * kStep;

    // Shift+Wheel panning horizontally is the DAW convention. Also honour
    // a horizontal-dominant touchpad gesture.
    const bool horizontalIntent = e.mods.isShiftDown() || std::abs(dx) > std::abs(dy);

    if (horizontalIntent)
    {
        const double delta = (std::abs(dx) > 1.0e-3) ? dx : dy;
        double newH = horizontalOffset - delta;
        newH = juce::jlimit(0.0, getMaxHorizontalOffset(), newH);
        horizontalScroll.setCurrentRangeStart(newH);
    }
    else
    {
        double newV = verticalOffset - dy;
        newV = juce::jlimit(0.0, getMaxVerticalOffset(), newV);
        verticalScroll.setCurrentRangeStart(newV);
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

    // ── Piano keys ──
    // Clipped to [0, headerHeight] .. [keyWidth, getHeight()-12] so a scrolled
    // key that would spill up into the header area can't bleed over the beat
    // numbers (which sit at y=0..headerHeight).
    {
        juce::Graphics::ScopedSaveState clipState(g);
        g.reduceClipRegion(0, headerHeight, keyWidth, gridHeight);

        int startKey = scrolledY / noteHeight;
        for (int i = startKey; i < numKeys; ++i)
        {
            int y = headerHeight + i * noteHeight - scrolledY;
            if (y > getHeight()) break;

            bool black = isBlackKey(i);

            // White/black key background
            g.setColour(black ? juce::Colours::black : juce::Colours::white);
            g.fillRect(0, y, keyWidth, noteHeight);

            // Key border
            g.setColour(juce::Colours::darkgrey);
            g.drawRect(0, y, keyWidth, noteHeight, 1);

            // Octave label on Cs
            if (!black)
            {
                int octave = (numKeys - 1 - i) / 12;
                int noteInOctave = (numKeys - 1 - i) % 12;
                if (noteInOctave == 0)
                {
                    g.setColour(juce::Colours::black);
                    g.setFont(10.0f);
                    g.drawText("C" + juce::String(octave), 2, y, keyWidth - 4, noteHeight,
                        juce::Justification::centredLeft);
                }
            }
        }
    }

    // ── Grid rows, beat lines, placed notes ──
    // All clipped to the grid area. A scrolled-up note/row/beat line that
    // would otherwise paint across the beat header or key column is trimmed
    // here.
    {
        juce::Graphics::ScopedSaveState clipState(g);
        g.reduceClipRegion(keyWidth, headerHeight, gridWidth, gridHeight);

        int startKey = scrolledY / noteHeight;
        for (int i = startKey; i < numKeys; ++i)
        {
            int y = headerHeight + i * noteHeight - scrolledY;
            if (y > getHeight()) break;

            bool black = isBlackKey(i);
            g.setColour(black ? juce::Colour(20, 20, 35) : juce::Colour(28, 28, 45));
            g.fillRect(keyWidth, y, gridWidth, noteHeight);

            g.setColour(juce::Colour(45, 45, 70));
            g.drawLine((float)keyWidth, (float)y,
                (float)(keyWidth + gridWidth), (float)y, 1.0f);
        }

        int startBeat = scrolledX / cellWidth;
        for (int b = startBeat; b <= numBeats; ++b)
        {
            int x = keyWidth + b * cellWidth - scrolledX;
            if (x > getWidth()) break;
            g.setColour(b % 4 == 0 ? juce::Colour(70, 70, 100) : juce::Colour(40, 40, 65));
            g.drawLine((float)x, (float)headerHeight,
                (float)x, (float)(getHeight() - 12), 1.0f);
        }

        for (auto& note : placedNotes)
        {
            int x = keyWidth + note.beat * cellWidth - scrolledX;
            int y = headerHeight + note.pitch * noteHeight - scrolledY;
            if (x + cellWidth < keyWidth || x > getWidth()) continue;
            if (y + noteHeight < headerHeight || y > getHeight()) continue;

            int noteW = cellWidth * note.duration;
            g.setColour(getNoteColour(note.pitch));
            g.fillRoundedRectangle((float)(x + 1), (float)(y + 1),
                (float)(noteW - 2), (float)(noteHeight - 2), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.drawRoundedRectangle((float)(x + 1), (float)(y + 1),
                (float)(noteW - 2), (float)(noteHeight - 2), 3.0f, 1.0f);
            if (note.lyric.isNotEmpty())
            {
                g.setColour(juce::Colours::white);
                g.setFont(10.0f);
                g.drawText(note.lyric, x + 2, y + 1, noteW - 4, noteHeight - 2,
                    juce::Justification::centred);
            }
            // Resize handle on right edge
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.fillRect(x + noteW - 4, y + 2, 3, noteHeight - 4);
        }
    }

    // ── Beat header ──
    // Drawn LAST so it sits on top of any grid content that might have rendered
    // right at the boundary. Fully opaque fill, then numbers.
    g.setColour(juce::Colour(30, 30, 50));
    g.fillRect(keyWidth, 0, gridWidth, headerHeight);
    g.setColour(juce::Colours::grey);
    g.setFont(11.0f);
    {
        juce::Graphics::ScopedSaveState clipState(g);
        g.reduceClipRegion(keyWidth, 0, gridWidth, headerHeight);

        int startBeat = scrolledX / cellWidth;
        for (int b = startBeat; b < numBeats; ++b)
        {
            int x = keyWidth + b * cellWidth - scrolledX;
            if (x > getWidth()) break;
            g.drawText(juce::String(b + 1), x + 2, 0, cellWidth, headerHeight,
                juce::Justification::centredLeft);
            g.setColour(juce::Colour(50, 50, 80));
            g.drawLine((float)x, 0.0f, (float)x, (float)headerHeight, 1.0f);
            g.setColour(juce::Colours::grey);
        }
    }

    // Top-left corner cover (where key column meets beat header)
    g.setColour(juce::Colour(15, 15, 25));
    g.fillRect(0, 0, keyWidth, headerHeight);
}

void PianoRollComponent::resized()
{
    constexpr int scrollBarThickness = 12;

    verticalScroll.setBounds(getWidth() - scrollBarThickness, headerHeight,
        scrollBarThickness, getHeight() - headerHeight - scrollBarThickness);
    horizontalScroll.setBounds(keyWidth, getHeight() - scrollBarThickness,
        getWidth() - keyWidth - scrollBarThickness, scrollBarThickness);

    // Range limits describe the full virtual content; clamp/push viewport state
    // into the scrollbars below.
    verticalScroll.setRangeLimits(0.0, (double)(numKeys * noteHeight));
    horizontalScroll.setRangeLimits(0.0, (double)(numBeats * cellWidth));

    // First valid resize: centre the view on C4 so the user isn't dropped into
    // the useless top-of-range region.
    if (!initialScrollApplied && getWidth() > 0 && getHeight() > 0)
    {
        centreVerticallyOnMidi(60);
        initialScrollApplied = true;
    }

    clampOffsetsToViewport();
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
// ============================================================================
//  Scroll geometry helpers
// ============================================================================

int PianoRollComponent::getViewportWidth() const
{
    constexpr int scrollBarThickness = 12;
    return std::max(0, getWidth() - keyWidth - scrollBarThickness);
}

int PianoRollComponent::getViewportHeight() const
{
    constexpr int scrollBarThickness = 12;
    return std::max(0, getHeight() - headerHeight - scrollBarThickness);
}

double PianoRollComponent::getMaxVerticalOffset() const
{
    const double total = (double)(numKeys * noteHeight);
    return std::max(0.0, total - (double)getViewportHeight());
}

double PianoRollComponent::getMaxHorizontalOffset() const
{
    const double total = (double)(numBeats * cellWidth);
    return std::max(0.0, total - (double)getViewportWidth());
}

void PianoRollComponent::clampOffsetsToViewport()
{
    const double maxV = getMaxVerticalOffset();
    const double maxH = getMaxHorizontalOffset();

    // Re-clamp offsets in case the window shrunk while we were scrolled down.
    if (verticalOffset > maxV)   verticalOffset = maxV;
    if (horizontalOffset > maxH) horizontalOffset = maxH;

    // Push the current viewport state into the scrollbars. The viewport size
    // (second arg of setCurrentRange) controls the thumb size; clamp it so it
    // can never exceed the total content range.
    const double vSize = std::min((double)getViewportHeight(),
        (double)(numKeys * noteHeight));
    const double hSize = std::min((double)getViewportWidth(),
        (double)(numBeats * cellWidth));
    verticalScroll.setCurrentRange(verticalOffset, std::max(1.0, vSize));
    horizontalScroll.setCurrentRange(horizontalOffset, std::max(1.0, hSize));
}

void PianoRollComponent::centreVerticallyOnMidi(int midi)
{
    // midi = (numKeys - 1 - gridPitch) + 12  =>  gridPitch = (numKeys + 11) - midi
    const int gridPitch = juce::jlimit(0, numKeys - 1, (numKeys + 11) - midi);
    const double targetY = (double)(gridPitch * noteHeight);
    const double vh = (double)getViewportHeight();

    double desired = targetY - (vh * 0.5) + (noteHeight * 0.5);
    desired = juce::jlimit(0.0, getMaxVerticalOffset(), desired);

    verticalOffset = desired;
    verticalScroll.setCurrentRangeStart(desired);
}

// ============================================================================
//  Lyric editor helpers
// ============================================================================

void PianoRollComponent::commitCurrentLyricEdit()
{
    // Idempotent: safe to call from multiple paths (onReturnKey, mouseDown
    // interception). No-op if nothing's being edited.
    if (editingNoteIndex < 0 || editingNoteIndex >= placedNotes.size())
    {
        editingNoteIndex = -1;
        lyricEditor.setVisible(false);
        return;
    }

    const juce::String lyric = lyricEditor.getText();
    placedNotes.getReference(editingNoteIndex).lyric = lyric;

    if (currentPatternId >= 0)
    {
        std::string patternIdStr = std::to_string(currentPatternId);
        std::string pitchStr = std::to_string(placedNotes[editingNoteIndex].pitch);
        std::string beatStr = std::to_string(placedNotes[editingNoteIndex].beat);
        const char* params[4] = { lyric.toRawUTF8(),
                                  patternIdStr.c_str(),
                                  pitchStr.c_str(),
                                  beatStr.c_str() };

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

void PianoRollComponent::discardCurrentLyricEdit()
{
    // Revert: hide the editor without touching the in-memory note lyric or DB.
    // The note itself (if newly created) stays — matches Escape-key behaviour.
    editingNoteIndex = -1;
    lyricEditor.setVisible(false);
    repaint();
}

void PianoRollComponent::positionLyricEditorForEditingNote()
{
    if (editingNoteIndex < 0 || editingNoteIndex >= placedNotes.size()) return;

    const auto& n = placedNotes[editingNoteIndex];
    const int x = keyWidth + n.beat * cellWidth - (int)horizontalOffset;
    const int y = headerHeight + n.pitch * noteHeight - (int)verticalOffset;
    // Use the note's full duration so the editor covers stretched notes.
    const int w = std::max(cellWidth, cellWidth * n.duration);
    lyricEditor.setBounds(x, y, w, noteHeight);
}

int PianoRollComponent::findNoteIndexAtMouse(const juce::MouseEvent& e) const
{
    if (e.x < keyWidth || e.y < headerHeight) return -1;

    const int gridX = e.x - keyWidth + (int)horizontalOffset;
    const int gridY = e.y - headerHeight + (int)verticalOffset;
    const int beat = gridX / cellWidth;
    const int pitch = gridY / noteHeight;

    if (beat < 0 || beat >= numBeats || pitch < 0 || pitch >= numKeys)
        return -1;

    // Resize-zone hits count as "on that note"
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        const auto& n = placedNotes[i];
        const int noteX = keyWidth + n.beat * cellWidth - (int)horizontalOffset;
        const int noteY = headerHeight + n.pitch * noteHeight - (int)verticalOffset;
        const int noteW = cellWidth * n.duration;
        juce::Rectangle<int> resizeZone(noteX + noteW - 6, noteY, 6, noteHeight);
        if (resizeZone.contains(e.x, e.y)) return i;
    }

    // Inside the note's body (covers multi-beat notes across their duration)
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        const auto& n = placedNotes[i];
        if (n.pitch == pitch && beat >= n.beat && beat < n.beat + n.duration)
            return i;
    }

    return -1;
}