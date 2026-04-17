#include "MainComponent.h"
#include "DatabaseManager.h"
#include "../DAW/DAWComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    DatabaseManager::get().testPostgresConnection();

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

    addAndMakeVisible(verifyButton);
    verifyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 120, 80));
    verifyButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    verifyButton.onClick = [this] { showVerifyDialog(); };

    verifyButton.setVisible(false);

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

    verifyButton.setBounds(10, 10, 120, 24);
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
    if (type == "Sign Up")
        dialog->addTextEditor("email", "", "Email:");
    dialog->addTextEditor("password", "", "Password:");
    dialog->getTextEditor("password")->setPasswordCharacter('*');

    dialog->addButton("Submit", 1);
    dialog->addButton("Cancel", 0);

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, dialog, type](int result)
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
                        juce::String email = dialog->getTextEditorContents("email");
                        if (email.isEmpty())
                        {
                            DBG("Email is empty");
                            return;
                        }

                        // Check email domain for educational account
                        juce::String userType = email.endsWithIgnoreCase("@southernct.edu") ? "educational" : "standard";

                        bool success = DatabaseManager::get().signUp(username, email, password, userType);
                        if (success)
                        {
                            DBG("Account created successfully!");
                            if (userType == "educational")
                            {
                                verifyButton.setVisible(true);
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::InfoIcon,
                                    "Account Created!",
                                    "A verification token has been sent to " + email + ".\n\nPlease check your email and use the Verify Account button to verify your account before logging in.");
                            }
                            else
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::AlertWindow::InfoIcon,
                                    "Account Created!",
                                    "Your account has been created! You can now log in.");
                            }
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Sign Up Failed",
                                "Username or email may already exist.");
                        }
                    }
                    else if (type == "Login")
                    {
                        bool success = DatabaseManager::get().login(username, password);
                        if (success)
                        {
                            DBG("Logged in successfully!");
                            if (onAuthenticationSuccess)
                                onAuthenticationSuccess(username);
                        }
                        else
                        {
                            juce::String error = DatabaseManager::get().getLastLoginError();
                            if (error.isEmpty())
                                error = "Login failed - incorrect username or password";

                            if (error.contains("verify"))
                                verifyButton.setVisible(true);

                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Login Failed",
                                error);
                        }
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

void MainComponent::showVerifyDialog()
{
    auto* dialog = new juce::AlertWindow("Verify Account",
        "Enter the verification token from your email:",
        juce::AlertWindow::NoIcon);

    dialog->addTextEditor("token", "", "Token:");
    dialog->addButton("Verify", 1);
    dialog->addButton("Cancel", 0);

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, dialog](int result)
            {
                if (result == 1)
                {
                    juce::String token = dialog->getTextEditorContents("token").trim();
                    if (token.isEmpty())
                        return;

                    bool success = DatabaseManager::get().verifyEmail(token);
                    if (success)
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::InfoIcon,
                            "Success!",
                            "Your email has been verified! You can now log in.");
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Verification Failed",
                            "Invalid or expired token. Please try again.");
                    }
                }
            }),
        true);
}

