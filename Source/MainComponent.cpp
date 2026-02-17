#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    setSize(600, 400);

    // Dark theme background
    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, juce::Colours::black);

    // Title
    titleLabel.setFont(juce::Font(40.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    // Buttons
    addAndMakeVisible(signupButton);
    addAndMakeVisible(loginButton);

    signupButton.onClick = [this] { showAuthDialog("Sign Up"); };
    loginButton.onClick = [this] { showAuthDialog("Login"); };

    // Button style
    signupButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 0, 120));
    signupButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    loginButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 80, 200));
    loginButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    // MouseListener
    signupButton.addMouseListener(this, true);
    loginButton.addMouseListener(this, true);

    // Glow lines setup
    int numLines = 5;
    float spacing = getHeight() / (float)numLines;
    lineOffsets.clear();
    for (int i = 0; i < numLines; ++i)
        lineOffsets.add(i * spacing);

    // Stars setup (independent of lines)
    for (int i = 0; i < 50; ++i)
    {
        Star s;
        s.x = juce::Random::getSystemRandom().nextFloat() * getWidth();
        s.y = juce::Random::getSystemRandom().nextFloat() * getHeight();
        s.size = 2.0f + juce::Random::getSystemRandom().nextFloat() * 4.0f; // 2–6 px
        s.alpha = 0.7f + juce::Random::getSystemRandom().nextFloat() * 0.3f; // initial twinkle
        stars.add(s);
    }

    // Start animation timer (60 FPS)
    startTimer(1000 / 60);
}

MainComponent::~MainComponent() {}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colours::black); // dark background

    // Draw stars (bright, twinkling)
    for (auto& s : stars)
    {
        g.setColour(juce::Colours::white.withAlpha(s.alpha));
        g.fillEllipse(s.x, s.y, s.size, s.size);
    }

    // Glow lines
    g.setColour(juce::Colours::purple.withMultipliedAlpha(0.2f));
    for (auto offset : lineOffsets)
    {
        float y = fmod(offset + glowPhase, (float)getHeight());
        g.drawLine(0.0f, y, (float)getWidth(), y, 2.0f);
    }

    // Title glow effect
    g.setFont(juce::Font(50.0f, juce::Font::bold));
    juce::ColourGradient grad(
        juce::Colours::hotpink, getWidth() / 2.0f, getHeight() / 3.0f,
        juce::Colours::cyan, getWidth() / 2.0f, getHeight() / 3.0f + 100,
        true
    );
    g.setGradientFill(grad);
    g.drawText("VocalNite", getLocalBounds().removeFromTop(200), juce::Justification::centred, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    titleLabel.setBounds(area.removeFromTop(200));

    auto buttonArea = area.removeFromTop(50).reduced(100, 0);
    signupButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(10));
    loginButton.setBounds(buttonArea.reduced(10));
}

void MainComponent::timerCallback()
{
    // Animate glow lines
    glowPhase += 1.0f; // speed of lines
    if (glowPhase > getHeight())
        glowPhase = 0.0f;

    // Animate stars: move slowly down and wrap around
    for (auto& s : stars)
    {
        s.y += 0.3f; // slower than lines
        if (s.y > getHeight())
            s.y = 0.0f; // wrap to top

        // Twinkle alpha
        s.alpha = 0.5f + 0.5f * std::sin(glowPhase * 0.01f + s.x + s.y);
    }

    repaint();
}

void MainComponent::showAuthDialog(const juce::String& type)
{
    auto* dialog = new juce::AlertWindow(type,
        "Enter your credentials",
        juce::AlertWindow::NoIcon);

    dialog->addTextEditor("username", "", "Username:");
    dialog->addTextEditor("password", "", "Password:");
    dialog->getTextEditor("password")->setPasswordCharacter('*');

    dialog->addButton("Submit", 1);
    dialog->addButton("Cancel", 0);

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [](int result)
            {
                // handle result if needed
            }),
            true);
}

//==============================================================================
// Hover effects
void MainComponent::mouseEnter(const juce::MouseEvent& event)
{
    if (event.eventComponent == &signupButton)
        signupButton.setAlpha(0.8f);
    else if (event.eventComponent == &loginButton)
        loginButton.setAlpha(0.8f);
}

void MainComponent::mouseExit(const juce::MouseEvent& event)
{
    if (event.eventComponent == &signupButton)
        signupButton.setAlpha(1.0f);
    else if (event.eventComponent == &loginButton)
        loginButton.setAlpha(1.0f);
}