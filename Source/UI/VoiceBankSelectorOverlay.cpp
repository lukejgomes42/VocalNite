#include "VoiceBankSelectorOverlay.h"

// ═══════════════════════════════════════════════════════════════════════════════
//  Easing helpers
// ═══════════════════════════════════════════════════════════════════════════════
namespace
{
    inline float easeOutCubic(float t) { t = juce::jlimit(0.0f, 1.0f, t); float inv = 1.0f - t; return 1.0f - inv * inv * inv; }
    inline float easeInCubic(float t) { t = juce::jlimit(0.0f, 1.0f, t); return t * t * t; }
    inline float easeOutQuint(float t) { t = juce::jlimit(0.0f, 1.0f, t); float inv = 1.0f - t; return 1.0f - inv * inv * inv * inv * inv; }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CharacterCard
// ═══════════════════════════════════════════════════════════════════════════════

VoiceBankSelectorOverlay::CharacterCard::CharacterCard()
{
    selectButton.setButtonText("SELECT");
    selectButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 0, 90));
    selectButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(120, 30, 160));
    selectButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    selectButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    selectButton.onClick = [this]() { if (onSelectClicked) onSelectClicked(); };
    addAndMakeVisible(selectButton);
}

void VoiceBankSelectorOverlay::CharacterCard::setInfo(const VoiceBankSelectorOverlay::BankInfo& i, bool isActive)
{
    info = i;
    active = isActive;

    // Attempt to decode the portrait. If the file is missing or invalid,
    // portraitImage stays null and drawPortrait falls back to the big letter.
    portraitImage = juce::Image();
    if (info.portraitFile != juce::File() && info.portraitFile.existsAsFile())
    {
        juce::Image img = juce::ImageFileFormat::loadFrom(info.portraitFile);
        if (img.isValid())
            portraitImage = img;
        else
            DBG("CharacterCard: failed to decode portrait " + info.portraitFile.getFullPathName());
    }

    // Re-tint the button to the character theme
    juce::Colour baseCol = info.themeColour.withSaturation(0.9f).withBrightness(0.35f);
    juce::Colour overCol = info.themeColour.withSaturation(1.0f).withBrightness(0.55f);
    selectButton.setColour(juce::TextButton::buttonColourId, baseCol);
    selectButton.setColour(juce::TextButton::buttonOnColourId, overCol);

    if (active)
    {
        selectButton.setButtonText("ACTIVE");
        selectButton.setEnabled(false);
    }
    else
    {
        selectButton.setButtonText("SELECT");
        selectButton.setEnabled(true);
    }
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::setIsActive(bool a) { setInfo(info, a); }

void VoiceBankSelectorOverlay::CharacterCard::setPulsePhase(float p)
{
    pulsePhase = p;
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::setEntrancePhase(float p)
{
    entrancePhase = p;
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::triggerSelectFlash()
{
    flashing = true;
    flashPhase = 0.0f;
    selectButton.setEnabled(false);
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::tickFlash(float delta)
{
    if (!flashing) return;
    flashPhase = juce::jlimit(0.0f, 1.0f, flashPhase + delta);
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::mouseEnter(const juce::MouseEvent&)
{
    mouseHovering = true;
    repaint();
}

void VoiceBankSelectorOverlay::CharacterCard::mouseExit(const juce::MouseEvent&)
{
    mouseHovering = false;
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Angular fighting-game frame path. Bevelled corners on the top-outer and
//  bottom-inner corners — which way the bevels tilt depends on which side
//  of the screen the card is on (mirror-image for left vs right).
// ─────────────────────────────────────────────────────────────────────────────
juce::Path VoiceBankSelectorOverlay::CharacterCard::buildFramePath(juce::Rectangle<float> r, float bevel) const
{
    juce::Path p;
    const float x1 = r.getX();
    const float x2 = r.getRight();
    const float y1 = r.getY();
    const float y2 = r.getBottom();
    const float b = bevel;

    if (leftSide)
    {
        // left card — top-right bevel, bottom-left bevel
        p.startNewSubPath(x1, y1);
        p.lineTo(x2 - b, y1);
        p.lineTo(x2, y1 + b);
        p.lineTo(x2, y2);
        p.lineTo(x1 + b, y2);
        p.lineTo(x1, y2 - b);
        p.closeSubPath();
    }
    else
    {
        // right card — mirror (top-left bevel, bottom-right bevel)
        p.startNewSubPath(x1 + b, y1);
        p.lineTo(x2, y1);
        p.lineTo(x2, y2 - b);
        p.lineTo(x2 - b, y2);
        p.lineTo(x1, y2);
        p.lineTo(x1, y1 + b);
        p.closeSubPath();
    }
    return p;
}

void VoiceBankSelectorOverlay::CharacterCard::drawFrame(juce::Graphics& g, juce::Rectangle<float> area)
{
    const float bevel = 18.0f;
    auto framePath = buildFramePath(area, bevel);

    // Gradient fill inside the angular frame.
    juce::ColourGradient grad(juce::Colour(28, 20, 48),
        area.getCentreX(), area.getY(),
        juce::Colour(14, 10, 28),
        area.getCentreX(), area.getBottom(), false);
    g.setGradientFill(grad);
    g.fillPath(framePath);

    // Subtle diagonal pattern layered in
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(framePath);
        g.setColour(info.themeColour.withAlpha(0.04f));
        for (float x = area.getX() - area.getHeight(); x < area.getRight(); x += 14.0f)
        {
            juce::Path line;
            line.startNewSubPath(x, area.getBottom());
            line.lineTo(x + area.getHeight(), area.getY());
            g.strokePath(line, juce::PathStrokeType(2.0f));
        }
    }

    // Glow behind the frame when hovered / active
    const float glowBase = active ? 0.35f : 0.0f;
    const float glowHover = mouseHovering ? 0.45f : 0.0f;
    const float pulseMod = 0.15f * (0.5f + 0.5f * std::sin(pulsePhase * juce::MathConstants<float>::twoPi));
    const float glow = juce::jlimit(0.0f, 1.0f, glowBase + glowHover + (mouseHovering ? pulseMod : 0.0f));

    if (glow > 0.001f)
    {
        for (int i = 4; i >= 1; --i)
        {
            g.setColour(info.themeColour.withAlpha(glow * 0.12f * (float)i));
            auto exp = area.expanded((float)i * 2.5f);
            g.strokePath(buildFramePath(exp, bevel + (float)i * 2.0f),
                juce::PathStrokeType(2.0f));
        }
    }

    // Frame outline — thicker / brighter when hovered or active
    float outlineAlpha = 0.55f;
    float outlineThick = 1.6f;
    if (mouseHovering) { outlineAlpha = 0.95f; outlineThick = 2.6f; }
    else if (active) { outlineAlpha = 0.8f;  outlineThick = 2.2f; }

    g.setColour(info.themeColour.withAlpha(outlineAlpha));
    g.strokePath(framePath, juce::PathStrokeType(outlineThick));
}

void VoiceBankSelectorOverlay::CharacterCard::drawPortrait(juce::Graphics& g, juce::Rectangle<float> portrait)
{
    // Clip everything we draw to the portrait rect
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(portrait.toNearestInt());

    // Deep background gradient (character-tinted)
    juce::ColourGradient bg(info.themeColour.withAlpha(0.18f),
        portrait.getCentreX(), portrait.getY(),
        juce::Colours::black.withAlpha(0.95f),
        portrait.getCentreX(), portrait.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRect(portrait);

    const float cx = portrait.getCentreX();
    const float cy = portrait.getCentreY();
    const float baseR = juce::jmin(portrait.getWidth(), portrait.getHeight()) * 0.35f;
    const float pulseSine = 0.5f + 0.5f * std::sin(pulsePhase * juce::MathConstants<float>::twoPi);
    const float hoverMul = mouseHovering ? 1.08f : 1.0f;
    const float r = baseR * hoverMul;

    // Outer pulsing rings (the "energy" behind the character)
    for (int i = 5; i >= 1; --i)
    {
        const float ringR = r + (float)i * (10.0f + 4.0f * pulseSine);
        const float alpha = (mouseHovering ? 0.14f : 0.08f) * (1.0f - (float)i / 6.0f);
        g.setColour(info.themeColour.withAlpha(alpha));
        g.drawEllipse(cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, 2.0f);
    }

    // Main circle with radial gradient
    juce::ColourGradient innerGrad(info.themeColour.withBrightness(0.85f),
        cx - r * 0.2f, cy - r * 0.3f,
        juce::Colours::black,
        cx, cy, true);
    innerGrad.addColour(0.55, info.themeColour.withSaturation(0.9f).withBrightness(0.4f));
    g.setGradientFill(innerGrad);
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    // Portrait image OR big initial letter. Image takes priority when valid.
    if (portraitImage.isValid())
    {
        // Clip image draw to the inner circle so it can't escape the frame.
        juce::Graphics::ScopedSaveState ss2(g);
        juce::Path clip;
        clip.addEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.reduceClipRegion(clip);

        // Cover-fit: scale the image so it FILLS the circle bounds while
        // preserving aspect ratio. Centered; edges cropped by the clip path.
        const float iw = (float)portraitImage.getWidth();
        const float ih = (float)portraitImage.getHeight();
        const float diameter = r * 2.0f;
        const float scale = juce::jmax(diameter / iw, diameter / ih);
        const float drawW = iw * scale;
        const float drawH = ih * scale;
        const float drawX = cx - drawW * 0.5f;
        const float drawY = cy - drawH * 0.5f;

        g.drawImage(portraitImage,
            juce::Rectangle<float>(drawX, drawY, drawW, drawH),
            juce::RectanglePlacement::stretchToFit);

        // Subtle inner theme tint for cohesion with rings + rim.
        g.setColour(info.themeColour.withAlpha(0.15f));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }
    else
    {
        // Big initial letter in the center (fallback when no portrait file exists)
        juce::Font bigFont(r * 1.3f, juce::Font::bold);
        g.setFont(bigFont);

        // Shadow behind the letter
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawText(info.initial,
            juce::Rectangle<float>(cx - r + 4, cy - r + 6, r * 2.0f, r * 2.0f),
            juce::Justification::centred);

        // White body
        g.setColour(juce::Colours::white);
        g.drawText(info.initial,
            juce::Rectangle<float>(cx - r, cy - r, r * 2.0f, r * 2.0f),
            juce::Justification::centred);

        // Hot-color accent overlaid at low alpha for a subtle tint
        g.setColour(info.themeColour.withAlpha(0.25f));
        g.drawText(info.initial,
            juce::Rectangle<float>(cx - r, cy - r, r * 2.0f, r * 2.0f),
            juce::Justification::centred);
    }

    // Rim highlight — drawn AFTER the image so it frames the portrait nicely
    g.setColour(info.themeColour.withAlpha(0.9f));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 2.5f);

    // Scan lines across the portrait (CRT vibe)
    g.setColour(juce::Colours::black.withAlpha(0.22f));
    for (float yy = portrait.getY(); yy < portrait.getBottom(); yy += 3.0f)
        g.fillRect(portrait.getX(), yy, portrait.getWidth(), 1.0f);

    // Active badge (top-left corner)
    if (active)
    {
        auto badge = juce::Rectangle<float>(portrait.getX() + 10.0f, portrait.getY() + 10.0f, 90.0f, 24.0f);
        g.setColour(info.themeColour.withAlpha(0.9f));
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("\xE2\x9C\x93 ACTIVE"), badge, juce::Justification::centred);
    }
}

void VoiceBankSelectorOverlay::CharacterCard::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat().reduced(2.0f);

    // Entrance offset: slide from the card's side to center
    const float dir = leftSide ? -1.0f : 1.0f;
    const float slideDist = (1.0f - entrancePhase) * 120.0f * dir;
    const float alpha = entrancePhase;

    juce::Graphics::ScopedSaveState ss(g);
    g.addTransform(juce::AffineTransform::translation(slideDist, 0.0f));

    // Hover scale (grow slightly toward center)
    if (mouseHovering)
    {
        auto c = full.getCentre();
        g.addTransform(juce::AffineTransform::scale(1.025f, 1.025f, c.x, c.y));
    }

    // Fade in with the entrance
    if (alpha < 0.999f)
    {
        g.beginTransparencyLayer(alpha);
    }

    // Frame
    drawFrame(g, full);

    // Portrait occupies top ~65% of the card
    auto portrait = full.reduced(12.0f, 12.0f);
    portrait.removeFromBottom(full.getHeight() * 0.38f);
    drawPortrait(g, portrait);

    // Name + description region (bottom 38% minus button)
    auto textZone = full;
    textZone.removeFromTop(full.getHeight() * 0.62f);
    textZone = textZone.reduced(14.0f, 4.0f);
    textZone.removeFromBottom(48.0f);   // reserve space for select button

    g.setColour(info.themeColour.withSaturation(0.8f));
    g.setFont(juce::Font(26.0f, juce::Font::bold));
    auto nameArea = textZone.removeFromTop(34.0f);
    g.drawText(info.displayName.toUpperCase(), nameArea, juce::Justification::centred);

    // Divider under the name
    {
        auto divArea = textZone.removeFromTop(10.0f);
        g.setColour(info.themeColour.withAlpha(0.5f));
        g.drawLine(divArea.getX() + 30.0f, divArea.getCentreY(),
            divArea.getRight() - 30.0f, divArea.getCentreY(), 1.2f);
    }

    g.setColour(juce::Colour(220, 220, 235));
    g.setFont(juce::Font(13.5f, juce::Font::italic));
    g.drawFittedText(info.description, textZone.toNearestInt(),
        juce::Justification::centredTop, 2);

    // Flash burst — massive white flash that overlays the whole card
    if (flashing)
    {
        const float ph = flashPhase;
        // The burst: quick brightening then fade out
        float burstAlpha;
        if (ph < 0.25f) burstAlpha = ph / 0.25f;
        else            burstAlpha = 1.0f - (ph - 0.25f) / 0.75f;
        burstAlpha = juce::jlimit(0.0f, 1.0f, burstAlpha);

        // Zooming halo behind the frame
        const float haloR = 40.0f + ph * 400.0f;
        g.setColour(juce::Colours::white.withAlpha(burstAlpha * 0.7f));
        auto c = full.getCentre();
        g.drawEllipse(c.x - haloR, c.y - haloR, haloR * 2.0f, haloR * 2.0f, 4.0f);

        // Color-tinted outer ring
        g.setColour(info.themeColour.withAlpha(burstAlpha * 0.9f));
        g.drawEllipse(c.x - haloR * 0.7f, c.y - haloR * 0.7f, haloR * 1.4f, haloR * 1.4f, 6.0f);

        // Full-card white wash
        g.setColour(juce::Colours::white.withAlpha(burstAlpha * 0.85f));
        g.fillPath(buildFramePath(full, 18.0f));
    }

    if (alpha < 0.999f)
        g.endTransparencyLayer();
}

void VoiceBankSelectorOverlay::CharacterCard::resized()
{
    auto r = getLocalBounds();
    // Select button at the bottom, centered
    auto btnH = 38;
    auto btnW = juce::jmin(180, r.getWidth() - 40);
    selectButton.setBounds(r.getCentreX() - btnW / 2,
        r.getBottom() - btnH - 16,
        btnW, btnH);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  VoiceBankSelectorOverlay
// ═══════════════════════════════════════════════════════════════════════════════

VoiceBankSelectorOverlay::VoiceBankSelectorOverlay()
{
    setInterceptsMouseClicks(true, true);

    titleLabel.setText("CHOOSE YOUR VOICE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(36.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("SELECT YOUR VOCAL BANK", juce::dontSendNotification);
    subtitleLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(180, 180, 210));
    subtitleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(subtitleLabel);

    emptyStateLabel.setText("No voice banks found in Resources/VoiceBank/", juce::dontSendNotification);
    emptyStateLabel.setFont(juce::Font(16.0f, juce::Font::italic));
    emptyStateLabel.setColour(juce::Label::textColourId, juce::Colour(200, 200, 220));
    emptyStateLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyStateLabel);

    closeButton.setButtonText("X");
    closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 20, 80));
    closeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(80, 30, 120));
    closeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    closeButton.onClick = [this]()
        {
            if (isClosing) return;
            setVisible(false);
        };
    addAndMakeVisible(closeButton);

    setWantsKeyboardFocus(true);
}

VoiceBankSelectorOverlay::~VoiceBankSelectorOverlay()
{
    stopTimer();
}

void VoiceBankSelectorOverlay::setAvailableBanks(const juce::Array<BankInfo>& banks,
    const juce::String& currentId)
{
    currentBankId = currentId;
    cards.clear();

    for (int i = 0; i < banks.size(); ++i)
    {
        auto* card = new CharacterCard();
        const auto& info = banks.getReference(i);
        const bool isActive = info.id == currentBankId;
        card->setInfo(info, isActive);
        card->setLeftSide(i == 0);
        card->onSelectClicked = [this, id = info.id]()
            {
                // Start the flash-burst on the selected card; after the burst
                // completes, timerCallback() fires onBankSelected and hides.
                if (isClosing) return;
                pendingBankId = id;
                isClosing = true;
                closingPhase = 0.0f;
                for (auto* c : cards)
                    if (c->getInfo().id == id)
                        c->triggerSelectFlash();
            };
        addAndMakeVisible(card);
        cards.add(card);
    }

    emptyStateLabel.setVisible(banks.isEmpty());

    layoutCards();
    repaint();
}

juce::Rectangle<int> VoiceBankSelectorOverlay::getTitleBounds() const
{
    return juce::Rectangle<int>(0, 30, getWidth(), 56);
}

juce::Rectangle<int> VoiceBankSelectorOverlay::getCardArea() const
{
    // Reserve the middle vertical band for cards
    int top = 120;
    int bottom = getHeight() - 40;
    return juce::Rectangle<int>(0, top, getWidth(), bottom - top);
}

juce::Rectangle<int> VoiceBankSelectorOverlay::getCardBounds(int index) const
{
    auto area = getCardArea();
    const int cardW = juce::jmin(300, (area.getWidth() - 220) / 2);
    const int cardH = juce::jmin(440, area.getHeight());
    const int cy = area.getCentreY();

    const int n = cards.size();
    if (n == 0) return {};
    if (n == 1)
    {
        return juce::Rectangle<int>(area.getCentreX() - cardW / 2,
            cy - cardH / 2, cardW, cardH);
    }

    // Two cards: left at cx - gap/2 - cardW, right at cx + gap/2
    const int gap = 180;
    const int leftX = area.getCentreX() - gap / 2 - cardW;
    const int rightX = area.getCentreX() + gap / 2;
    if (index == 0)
        return juce::Rectangle<int>(leftX, cy - cardH / 2, cardW, cardH);
    return juce::Rectangle<int>(rightX, cy - cardH / 2, cardW, cardH);
}

juce::Rectangle<int> VoiceBankSelectorOverlay::getVsBounds() const
{
    if (cards.size() < 2) return {};
    auto area = getCardArea();
    const int vsW = 160;
    const int vsH = 120;
    return juce::Rectangle<int>(area.getCentreX() - vsW / 2,
        area.getCentreY() - vsH / 2,
        vsW, vsH);
}

void VoiceBankSelectorOverlay::layoutCards()
{
    for (int i = 0; i < cards.size(); ++i)
    {
        cards[i]->setBounds(getCardBounds(i));
    }
}

void VoiceBankSelectorOverlay::resized()
{
    titleLabel.setBounds(getTitleBounds());
    subtitleLabel.setBounds(0, getTitleBounds().getBottom() + 2, getWidth(), 22);
    closeButton.setBounds(getWidth() - 48, 16, 32, 32);
    emptyStateLabel.setBounds(0, getHeight() / 2 - 20, getWidth(), 40);
    layoutCards();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────────────────────────────────
void VoiceBankSelectorOverlay::paintBackdrop(juce::Graphics& g)
{
    // Near-black vignetted backdrop
    g.fillAll(juce::Colour(6, 4, 14).withAlpha(0.97f));

    // Soft radial vignette centered on screen
    juce::ColourGradient radial(juce::Colour(40, 20, 70).withAlpha(0.55f),
        (float)getWidth() / 2.0f, (float)getHeight() / 2.0f,
        juce::Colours::black.withAlpha(0.95f),
        0.0f, (float)getHeight(), true);
    g.setGradientFill(radial);
    g.fillRect(getLocalBounds());

    // Animated diagonal energy stripes
    {
        const float strideBase = 60.0f;
        const float scroll = animPhase * strideBase;   // wraps naturally since animPhase wraps
        const float alpha = 0.05f;
        g.setColour(juce::Colours::hotpink.withAlpha(alpha));
        for (float x = -getHeight() - strideBase + scroll; x < getWidth() + strideBase; x += strideBase)
        {
            juce::Path line;
            line.startNewSubPath(x, (float)getHeight());
            line.lineTo(x + (float)getHeight(), 0.0f);
            g.strokePath(line, juce::PathStrokeType(2.0f));
        }
    }

    // Faint secondary grid of horizontal lines
    g.setColour(juce::Colours::hotpink.withAlpha(0.025f));
    for (int y = 0; y < getHeight(); y += 4)
        g.fillRect(0, y, getWidth(), 1);
}

void VoiceBankSelectorOverlay::paintScanlines(juce::Graphics& g)
{
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    for (int y = 0; y < getHeight(); y += 3)
        g.fillRect(0, y, getWidth(), 1);
}

void VoiceBankSelectorOverlay::paintVS(juce::Graphics& g)
{
    auto vs = getVsBounds();
    if (vs.isEmpty()) return;

    // Pulsing modulation
    const float pulse = 0.5f + 0.5f * std::sin(animPhase * juce::MathConstants<float>::twoPi * 2.0f);

    // Entrance scale
    const float scale = 0.4f + 0.6f * easeOutQuint(entrancePhase);
    auto centre = vs.getCentre().toFloat();

    juce::Graphics::ScopedSaveState ss(g);
    g.addTransform(juce::AffineTransform::scale(scale, scale, centre.x, centre.y));

    juce::Font vsFont(96.0f, juce::Font::bold);
    g.setFont(vsFont);

    // Glow halo — draw the text multiple times with increasing blur-ish offsets
    for (int i = 6; i >= 1; --i)
    {
        const float a = (0.03f + 0.04f * pulse) * (float)i;
        g.setColour(juce::Colours::hotpink.withAlpha(juce::jlimit(0.0f, 1.0f, a)));
        auto r = vs.toFloat().expanded((float)i * 2.0f);
        g.drawText("VS", r, juce::Justification::centred, false);
    }

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawText("VS", vs.toFloat().translated(4.0f, 6.0f), juce::Justification::centred, false);

    // Main hotpink body
    g.setColour(juce::Colours::hotpink);
    g.drawText("VS", vs.toFloat(), juce::Justification::centred, false);

    // White inner highlight
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    juce::Font innerFont(90.0f, juce::Font::bold);
    g.setFont(innerFont);
    g.drawText("VS", vs.toFloat(), juce::Justification::centred, false);
}

void VoiceBankSelectorOverlay::paint(juce::Graphics& g)
{
    paintBackdrop(g);
    paintScanlines(g);

    // VS between cards
    paintVS(g);

    // White closing-flash wash that covers the whole screen
    if (isClosing && closingPhase > 0.001f)
    {
        const float a = juce::jlimit(0.0f, 1.0f, closingPhase * 0.8f);
        g.setColour(juce::Colours::white.withAlpha(a));
        g.fillRect(getLocalBounds());
    }
}

void VoiceBankSelectorOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (isClosing) return;
    // Click outside any card (on the backdrop) dismisses the overlay.
    bool insideCard = false;
    for (auto* c : cards)
        if (c->getBounds().contains(e.getPosition()))
        {
            insideCard = true;
            break;
        }

    const bool onTitle = getTitleBounds().contains(e.getPosition());
    const bool onClose = closeButton.getBounds().contains(e.getPosition());
    if (!insideCard && !onTitle && !onClose)
        setVisible(false);
}

bool VoiceBankSelectorOverlay::keyPressed(const juce::KeyPress& key)
{
    if (isClosing) return false;
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible(false);
        return true;
    }
    return false;
}

void VoiceBankSelectorOverlay::visibilityChanged()
{
    if (isVisible())
    {
        entrancePhase = 0.0f;
        animPhase = 0.0f;
        closingPhase = 0.0f;
        isClosing = false;
        pendingBankId.clear();
        for (auto* c : cards) c->setEntrancePhase(0.0f);
        startTimerHz(45);
        grabKeyboardFocus();
    }
    else
    {
        stopTimer();
    }
}

void VoiceBankSelectorOverlay::timerCallback()
{
    // Drive the global pulse
    const float dt = 1.0f / 45.0f;
    animPhase += dt * 0.5f;   // ~2s cycle
    if (animPhase > 1.0f) animPhase -= 1.0f;

    // Entrance ease-in (~400ms)
    if (entrancePhase < 1.0f)
    {
        entrancePhase = juce::jlimit(0.0f, 1.0f, entrancePhase + dt * 2.5f);
        const float eased = easeOutCubic(entrancePhase);
        for (auto* c : cards) c->setEntrancePhase(eased);
    }

    // Update the cards' pulse phase
    for (auto* c : cards)
    {
        c->setPulsePhase(animPhase);
        c->tickFlash(dt * 1.5f);   // flash burst completes in ~0.66s
    }

    // Closing animation: wait for flashing card to finish, then fade overlay white
    if (isClosing)
    {
        closingPhase = juce::jlimit(0.0f, 1.0f, closingPhase + dt * 2.0f);
        if (closingPhase >= 1.0f)
        {
            const juce::String id = pendingBankId;
            auto cb = onBankSelected;
            setVisible(false);
            if (cb) cb(id);
            return;
        }
    }

    repaint();
}