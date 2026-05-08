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
    // Click-away commit: when the editor loses keyboard focus (user clicks
    // outside the editor entirely — e.g. transport bar, scrollbar, window
    // frame — anywhere our mouseDown isn't going to handle the commit),
    // save the lyric automatically. The suppressNextFocusLost flag is set
    // by the mouseDown "fluid-switch" path (commit old → open new) where
    // committing again from this async callback would clobber the new
    // editor. The flag is cleared on the next message-queue drain so the
    // very next genuine focus loss after a switch still commits.
    lyricEditor.onFocusLost = [this]()
        {
            if (suppressNextFocusLost) return;
            commitCurrentLyricEdit();
        };
    addAndMakeVisible(lyricEditor);

    loadNotes();

    // Tooltips are always on for all users now (used to be ed-mode gated).
    EducationalModeManager::getInstance().addListener(this);
    applyTooltips(true);

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
    return rowIsSharp(noteIndex);
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

        // Either case below will hide the current editor, which fires an
        // async focus-loss for the OLD editor. Suppress that one — committing
        // again from the async callback would clobber the freshly-opened
        // editor (or repeat the commit we already did synchronously).
        suppressNextFocusLost = true;
        juce::MessageManager::callAsync([this]() { suppressNextFocusLost = false; });

        if (idx >= 0)
            commitCurrentLyricEdit();   // fall through to normal handling
        else
        {
            // Click on empty grid: save the lyric (was previously discard).
            // This is the fix for "left-click after typing should save".
            commitCurrentLyricEdit();
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
                if (placedNotes[i].pitch == pitch && placedNotes[i].beat == beat)
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
    // Left-click: drag existing note / open lyric editor / create new
    // ------------------------------------------------------------------
    int gridX = e.x - keyWidth + (int)horizontalOffset;
    int gridY = e.y - headerHeight + (int)verticalOffset;

    if (e.x < keyWidth || e.y < headerHeight)
        return;

    int beat = gridX / cellWidth;
    int pitch = gridY / noteHeight;

    if (beat < 0 || beat >= numBeats || pitch < 0 || pitch >= numKeys)
        return;

    // Click on an existing note → begin drag (mouseUp decides click vs. drag)
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        const auto& n = placedNotes[i];
        if (n.pitch == pitch && n.beat == beat)
        {
            if (lyricEditor.isVisible())
            {
                suppressNextFocusLost = true;
                commitCurrentLyricEdit();
                juce::MessageManager::callAsync([this]() { suppressNextFocusLost = false; });
            }
            isDraggingNote = true;
            draggingNoteIndex = i;
            dragStartMouseX = e.x;
            dragStartMouseY = e.y;
            dragOriginalBeat = n.beat;
            dragOriginalPitch = n.pitch;
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
    if (isDraggingNote && draggingNoteIndex >= 0)
    {
        const int dx = std::abs(e.x - dragStartMouseX);
        const int dy = std::abs(e.y - dragStartMouseY);
        const bool wasDrag = (dx > 4 || dy > 4);

        if (!wasDrag)
        {
            // Treat as a plain click — open the lyric editor
            editingNoteIndex = draggingNoteIndex;
            const auto& n = placedNotes[draggingNoteIndex];
            lyricEditor.setText(n.lyric);
            positionLyricEditorForEditingNote();
            lyricEditor.setVisible(true);
            lyricEditor.grabKeyboardFocus();
            lyricEditor.selectAll();
        }
        else
        {
            // Persist the new position to DB using the original coords as key
            const auto& n = placedNotes[draggingNoteIndex];
            std::string patIdStr = std::to_string(currentPatternId);
            std::string newPitch = std::to_string(n.pitch);
            std::string newBeat = std::to_string(n.beat);
            std::string oldPitch = std::to_string(dragOriginalPitch);
            std::string oldBeat = std::to_string(dragOriginalBeat);
            const char* params[5] = { newPitch.c_str(), newBeat.c_str(),
                                      patIdStr.c_str(),
                                      oldPitch.c_str(), oldBeat.c_str() };
            PGresult* res = PQexecParams(DatabaseManager::get().db(),
                "UPDATE PatternNotes SET pitch=$1, beat=$2 "
                "WHERE pattern_id=$3 AND pitch=$4 AND beat=$5",
                5, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(res) != PGRES_COMMAND_OK)
                DBG("Note move error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
            PQclear(res);
        }

        isDraggingNote = false;
        draggingNoteIndex = -1;
    }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDraggingNote || draggingNoteIndex < 0)
        return;

    const int gridX = e.x - keyWidth + (int)horizontalOffset;
    const int gridY = e.y - headerHeight + (int)verticalOffset;

    const int newBeat = juce::jlimit(0, numBeats - 1, gridX / cellWidth);
    const int newPitch = juce::jlimit(0, numKeys - 1, gridY / noteHeight);

    auto& n = placedNotes.getReference(draggingNoteIndex);
    if (n.beat != newBeat || n.pitch != newPitch)
    {
        n.beat = newBeat;
        n.pitch = newPitch;

        if (lyricEditor.isVisible())
            positionLyricEditorForEditingNote();

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

            const bool sharp = rowIsSharp(i);
            const bool bankNote = rowIsBankNote(i);

            // Key background: F# rows darker, bank-folder rows pinker
            juce::Colour keyBg = sharp ? juce::Colour(18, 8, 32)
                : juce::Colour(35, 15, 58);
            if (bankNote) keyBg = keyBg.brighter(0.18f);
            g.setColour(keyBg);
            g.fillRect(0, y, keyWidth, noteHeight);

            // Subtle separator line
            g.setColour(juce::Colour(60, 30, 90));
            g.drawLine(0.0f, (float)(y + noteHeight - 1),
                (float)keyWidth, (float)(y + noteHeight - 1), 1.0f);

            // Bank-note accent strip on left edge
            if (bankNote)
            {
                g.setColour(juce::Colours::hotpink.withAlpha(0.7f));
                g.fillRect(0, y + 2, 3, noteHeight - 4);
            }

            // Note name label
            g.setColour(bankNote ? juce::Colours::hotpink
                : (sharp ? juce::Colour(160, 120, 200)
                    : juce::Colour(200, 170, 230)));
            g.setFont(juce::Font(10.0f, sharp ? juce::Font::plain : juce::Font::bold));
            g.drawText(kRowNames[i], 6, y, keyWidth - 8, noteHeight,
                juce::Justification::centredLeft);
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

            g.setColour(getNoteColour(note.pitch));
            g.fillRoundedRectangle((float)(x + 1), (float)(y + 1),
                (float)(cellWidth - 2), (float)(noteHeight - 2), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.drawRoundedRectangle((float)(x + 1), (float)(y + 1),
                (float)(cellWidth - 2), (float)(noteHeight - 2), 3.0f, 1.0f);
            if (note.lyric.isNotEmpty())
            {
                g.setColour(juce::Colours::white);
                g.setFont(10.0f);
                g.drawText(note.lyric, x + 2, y + 1, cellWidth - 4, noteHeight - 2,
                    juce::Justification::centred);
            }
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

    // ── Dynamic row height ────────────────────────────────────────────────
    // 15 rows × 28px = 420px. On a 600px+ window, that leaves dead space at
    // the bottom. Recompute noteHeight to fill the available grid area while
    // staying within sensible bounds.
    const int availableH = juce::jmax(0, getHeight() - headerHeight - scrollBarThickness);
    const int idealH = (numKeys > 0) ? availableH / numKeys : 28;
    noteHeight = juce::jlimit(28, 56, idealH);

    verticalScroll.setBounds(getWidth() - scrollBarThickness, headerHeight,
        scrollBarThickness, getHeight() - headerHeight - scrollBarThickness);
    horizontalScroll.setBounds(keyWidth, getHeight() - scrollBarThickness,
        getWidth() - keyWidth - scrollBarThickness, scrollBarThickness);

    verticalScroll.setRangeLimits(0.0, (double)(numKeys * noteHeight));
    horizontalScroll.setRangeLimits(0.0, (double)(numBeats * cellWidth));

    // First valid resize: centre on C4
    if (!initialScrollApplied && getWidth() > 0 && getHeight() > 0)
    {
        centreVerticallyOnMidi(60);
        initialScrollApplied = true;
    }

    clampOffsetsToViewport();

    // Reposition the lyric editor if it's open — its row coords depend on noteHeight
    if (lyricEditor.isVisible())
        positionLyricEditorForEditingNote();
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
//  Tooltips — always-on for ALL users (independent of ed-mode toggle).
//  Kept as a Listener so we still get the educationalModeChanged callback,
//  but the body just re-applies tooltips unconditionally.
// ============================================================================

void PianoRollComponent::educationalModeChanged(bool /*isEnabled*/)
{
    applyTooltips(true);   // arg is ignored; tooltips are always on
}

void PianoRollComponent::applyTooltips(bool /*eduEnabled — ignored*/)
{
    // The whole roll explains tile placement + row/column semantics
    setTooltip(TooltipRegistry::get("pianoRollTile"));
    // The inline text editor explains lyric → phoneme conversion
    lyricEditor.setTooltip(TooltipRegistry::get("lyricInput"));
}
// ============================================================================
//  Scroll geometry helpers
// ============================================================================

int PianoRollComponent::getViewportWidth() const
{
    constexpr int scrollBarThickness = 12;
    return juce::jmax(0, getWidth() - keyWidth - scrollBarThickness);
}

int PianoRollComponent::getViewportHeight() const
{
    constexpr int scrollBarThickness = 12;
    return juce::jmax(0, getHeight() - headerHeight - scrollBarThickness);
}

double PianoRollComponent::getMaxVerticalOffset() const
{
    const double total = (double)(numKeys * noteHeight);
    return juce::jmax(0.0, total - (double)getViewportHeight());
}

double PianoRollComponent::getMaxHorizontalOffset() const
{
    const double total = (double)(numBeats * cellWidth);
    return juce::jmax(0.0, total - (double)getViewportWidth());
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
    const double vSize = juce::jmin((double)getViewportHeight(),
        (double)(numKeys * noteHeight));
    const double hSize = juce::jmin((double)getViewportWidth(),
        (double)(numBeats * cellWidth));
    verticalScroll.setCurrentRange(verticalOffset, juce::jmax(1.0, vSize));
    horizontalScroll.setCurrentRange(horizontalOffset, juce::jmax(1.0, hSize));
}

void PianoRollComponent::centreVerticallyOnMidi(int midi)
{
    // Find the row whose MIDI pitch is closest to the requested value.
    int bestRow = 0, bestDist = 999;
    for (int i = 0; i < numKeys; ++i)
    {
        int d = std::abs(kRowMidi[i] - midi);
        if (d < bestDist) { bestDist = d; bestRow = i; }
    }

    const double targetY = (double)(bestRow * noteHeight);
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
    lyricEditor.setBounds(x, y, cellWidth, noteHeight);
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

    // Inside the note's cell
    for (int i = placedNotes.size() - 1; i >= 0; --i)
    {
        const auto& n = placedNotes[i];
        if (n.pitch == pitch && n.beat == beat)
            return i;
    }

    return -1;
}