#include "MainComponent.h"
#include "DatabaseManager.h"

//==============================================================================
MainComponent::MainComponent()
{
    DatabaseManager::get().testDB();

    setSize(600, 400);
    starSystem.setBounds(getWidth(), getHeight());
    glowLines.setBounds(getWidth(), getHeight());

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

    // Start animation timer (60 FPS)
    startTimer(1000 / 60);
}

MainComponent::~MainComponent() {}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colours::black); // dark background

    starSystem.draw(g);
    glowLines.draw(g);

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
    starSystem.update(0.0f, getHeight());
    glowLines.update();
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
            [dialog, type](int result)
            {
                if (result == 1)
                {
                    juce::String username = dialog->getTextEditorContents("username");
                    juce::String password = dialog->getTextEditorContents("password");

                    if (username.isEmpty() || password.isEmpty())
                    {
                        DBG("Username or password is empty");
                        return;
                    }

                    if (type == "Sign Up")
                    {
                        bool success = DatabaseManager::get().signUp(username, password, "standard");
                        if (success)
                            DBG("Account created successfully!");
                        else
                            DBG("Sign up failed - username may already exist");
                    }
                    else if (type == "Login")
                    {
                        bool success = DatabaseManager::get().login(username, password);
                        if (success)
                            DBG("Logged in successfully!");
                        else
                            DBG("Login failed - incorrect username or password");
                    }
                }
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