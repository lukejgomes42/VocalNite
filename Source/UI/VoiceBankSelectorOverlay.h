#pragma once
#include <JuceHeader.h>

// ───────────────────────────────────────────────────────────────────────────────
//  VoiceBankSelectorOverlay
//
//  Full-screen modal overlay shown inside DAWComponent. Mimics a fighting-game
//  "select your fighter" screen — two character cards side-by-side with a large
//  VS in the middle, angular frames, animated entrance, pulsing portrait glow,
//  and a flash-burst when a card is chosen. Owned by DAWComponent (sibling of
//  the existing helpOverlay / loadingOverlay).
//
//  Usage:
//      overlay.setAvailableBanks({ aaronInfo, utauInfo }, "aaron");
//      overlay.onBankSelected = [](juce::String id) { ... };
//      overlay.setVisible(true);
// ───────────────────────────────────────────────────────────────────────────────

class VoiceBankSelectorOverlay : public juce::Component,
    private juce::Timer
{
public:
    struct BankInfo
    {
        juce::String id;            // "aaron", "utau" ...
        juce::String displayName;   // "Aaron"
        juce::String description;   // "The Almighty Aaron..."
        juce::String initial;       // "A" — drawn huge inside the portrait circle (fallback when no image)
        juce::Colour themeColour;   // hotpink, cyan, etc.
        juce::File   bankFolder;    // absolute path on disk
        juce::File   portraitFile;  // optional: portrait.png / .jpg / .jpeg in bankFolder.
        // If invalid/missing, the big `initial` letter is drawn instead.
    };

    VoiceBankSelectorOverlay();
    ~VoiceBankSelectorOverlay() override;

    // Supply the list of banks to show (order matters: first is left card, second is right).
    // If 0 banks are passed, the overlay shows an empty-state message.
    // If 1 bank is passed, it's shown centered with no VS text.
    void setAvailableBanks(const juce::Array<BankInfo>& banks,
        const juce::String& currentBankId);

    // Fired after the flash-burst animation completes. The DAW should then
    // swap the voice bank. Parameter is the selected bank's id.
    std::function<void(juce::String /*bankId*/)> onBankSelected;

    // Component
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    // ───── CharacterCard ────────────────────────────────────────────────────
    class CharacterCard : public juce::Component
    {
    public:
        CharacterCard();

        void setInfo(const VoiceBankSelectorOverlay::BankInfo& info, bool isActive);
        void setPulsePhase(float phase01);      // 0..1, drives portrait glow
        void setEntrancePhase(float phase01);   // 0..1, drives slide/fade in
        void setLeftSide(bool isLeft) { leftSide = isLeft; }

        void triggerSelectFlash();              // starts the select burst
        bool isFlashing() const { return flashing; }
        bool isFlashComplete() const { return flashing && flashPhase >= 1.0f; }
        void tickFlash(float delta);            // call from parent timer

        // Visually marks this as the currently loaded bank (badge + disabled button).
        void setIsActive(bool active);

        std::function<void()> onSelectClicked;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseEnter(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;

        const VoiceBankSelectorOverlay::BankInfo& getInfo() const { return info; }

    private:
        void drawPortrait(juce::Graphics& g, juce::Rectangle<float> area);
        void drawFrame(juce::Graphics& g, juce::Rectangle<float> area);
        juce::Path buildFramePath(juce::Rectangle<float> area, float bevel) const;

        VoiceBankSelectorOverlay::BankInfo info;
        juce::Image portraitImage;        // cached decoded portrait (null if none / failed load)
        bool active = false;
        bool mouseHovering = false;
        bool leftSide = true;

        bool  flashing = false;
        float flashPhase = 0.0f;   // 0..1, eased
        float pulsePhase = 0.0f;   // 0..1 cycles
        float entrancePhase = 0.0f;// 0..1 once

        juce::TextButton selectButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CharacterCard)
    };

    // ───── Layout helpers ───────────────────────────────────────────────────
    juce::Rectangle<int> getCardArea() const;    // centered rect where cards live
    juce::Rectangle<int> getCardBounds(int index) const;
    juce::Rectangle<int> getVsBounds() const;
    juce::Rectangle<int> getTitleBounds() const;

    void layoutCards();
    void paintBackdrop(juce::Graphics& g);
    void paintScanlines(juce::Graphics& g);
    void paintVS(juce::Graphics& g);

    // ───── State ────────────────────────────────────────────────────────────
    juce::OwnedArray<CharacterCard> cards;
    juce::String currentBankId;

    juce::Label      titleLabel;
    juce::Label      subtitleLabel;
    juce::Label      emptyStateLabel;   // shown when no banks are available
    juce::TextButton closeButton;

    float animPhase = 0.0f;        // continuous 0..1..0 pulse
    float entrancePhase = 0.0f;    // 0 → 1 on show (eased)
    bool  isClosing = false;
    float closingPhase = 0.0f;     // 0 → 1 on select (eased)
    juce::String pendingBankId;    // stored during close animation

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceBankSelectorOverlay)
};