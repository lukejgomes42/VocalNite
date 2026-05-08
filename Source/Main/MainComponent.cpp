#include "MainComponent.h"
#include "../Database/DatabaseManager.h"

MainComponent::MainComponent()
{
    DatabaseManager::get().testPostgresConnection();

    setSize(600, 400);
    starSystem.setBounds(getWidth(), getHeight());
    glowLines.setBounds(getWidth(), getHeight());

    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, juce::Colours::black);

    // Title label — layout reservation only; text is drawn directly in paint().
    // Mouse interception disabled so clicks reach MainComponent::mouseDown.
    titleLabel.setFont(juce::Font(40.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(signupButton);
    addAndMakeVisible(loginButton);

    signupButton.onClick = [this] { showAuthDialog("Sign Up"); };
    loginButton.onClick = [this] { showAuthDialog("Login");   };

    verifyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 120, 80));
    verifyButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    verifyButton.onClick = [this] { showVerifyDialog(); };
    addAndMakeVisible(verifyButton);
    verifyButton.setVisible(false);   // hidden until an edu signup or failed-verify login

    signupButton.setColour(juce::TextButton::buttonColourId, juce::Colour(80, 0, 120));
    signupButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    loginButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0, 80, 200));
    loginButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    signupButton.addMouseListener(this, true);
    loginButton.addMouseListener(this, true);

    loadLogoImage();
    startTimer(1000 / 60);   // 60 fps
}

MainComponent::~MainComponent() {}

//==============================================================================
void MainComponent::loadLogoImage()
{
    // Walk up from the .exe to find the Resources folder that contains
    // cmudict.txt (the same heuristic used throughout the project), then
    // look for logo.png inside it.
    juce::File searchDir = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();

    juce::File resourcesDir;
    for (int i = 0; i < 10; ++i)
    {
        juce::File candidate = searchDir.getChildFile("Resources");
        if (candidate.isDirectory() && candidate.getChildFile("cmudict.txt").existsAsFile())
        {
            resourcesDir = candidate;
            break;
        }

        juce::File parent = searchDir.getParentDirectory();
        if (parent == searchDir) break;   // reached filesystem root
        searchDir = parent;
    }

    if (!resourcesDir.exists())
    {
        DBG("MainComponent::loadLogoImage - Resources folder not found");
        return;
    }

    juce::File logoFile = resourcesDir.getChildFile("logo.png");
    if (!logoFile.existsAsFile())
    {
        DBG("MainComponent::loadLogoImage - logo.png not found");
        return;
    }

    logoImage = juce::ImageFileFormat::loadFrom(logoFile);
    if (!logoImage.isValid())
        DBG("MainComponent::loadLogoImage - failed to decode logo.png");
}

//==============================================================================
//  Sparkle / swap animation
//==============================================================================
void MainComponent::triggerSwapAnimation()
{
    flashActive = true;
    flashStartTimeMs = juce::Time::getMillisecondCounter();
    logoAlpha = 0.0f;
    spawnSparkleBurst(70);
}

void MainComponent::spawnSparkleBurst(int count)
{
    auto& rng = juce::Random::getSystemRandom();

    const float cx = (float)titleHitArea.getCentreX();
    const float cy = (float)titleHitArea.getCentreY();

    for (int i = 0; i < count; ++i)
    {
        Sparkle s;
        s.x = cx + (rng.nextFloat() - 0.5f) * 30.0f;
        s.y = cy + (rng.nextFloat() - 0.5f) * 30.0f;

        const float angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float speed = 120.0f + rng.nextFloat() * 280.0f;
        s.vx = std::cos(angle) * speed;
        s.vy = std::sin(angle) * speed - 40.0f;   // slight upward bias

        s.age = 0.0f;
        s.lifetime = 0.7f + rng.nextFloat() * 0.9f;
        s.size = 2.5f + rng.nextFloat() * 4.5f;

        // Theme palette: hotpink / cyan / white, weighted toward hotpink/cyan
        const int pick = rng.nextInt(5);
        if (pick < 2) s.colour = juce::Colours::hotpink;
        else if (pick < 4) s.colour = juce::Colours::cyan;
        else               s.colour = juce::Colours::white;

        sparkles.add(s);
    }
}

void MainComponent::drawSparkle(juce::Graphics& g, const Sparkle& s) const
{
    const float ageRatio = juce::jlimit(0.0f, 1.0f, s.age / s.lifetime);
    float alpha = (1.0f - ageRatio);
    alpha *= alpha;   // ease-out fade
    if (alpha <= 0.01f) return;

    const float size = s.size * (0.5f + (1.0f - ageRatio) * 0.5f);

    // Outer soft glow halo
    g.setColour(s.colour.withAlpha(alpha * 0.3f));
    g.fillEllipse(s.x - size * 1.8f, s.y - size * 1.8f, size * 3.6f, size * 3.6f);

    // 4-pointed star (vertical + horizontal bars)
    g.setColour(s.colour.withAlpha(alpha));
    g.fillEllipse(s.x - size * 0.18f, s.y - size * 1.3f, size * 0.36f, size * 2.6f);
    g.fillEllipse(s.x - size * 1.3f, s.y - size * 0.18f, size * 2.6f, size * 0.36f);

    // Bright white core
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.fillEllipse(s.x - size * 0.25f, s.y - size * 0.25f, size * 0.5f, size * 0.5f);
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    starSystem.draw(g);
    glowLines.draw(g);

    auto titleArea = getLocalBounds().removeFromTop(200);
    titleHitArea = titleArea;

    if (showLogo)
    {
        if (logoImage.isValid())
        {
            constexpr int logoSize = 370;   // bounding box in pixels (aspect preserved)
            auto drawArea = juce::Rectangle<int>(logoSize, logoSize)
                .withCentre({ getWidth() / 2, titleArea.getCentreY() });

            g.setOpacity(logoAlpha);
            g.drawImageWithin(logoImage,
                drawArea.getX(), drawArea.getY(),
                drawArea.getWidth(), drawArea.getHeight(),
                juce::RectanglePlacement::centred, false);
            g.setOpacity(1.0f);
        }
    }
    else
    {
        // Gradient text title
        g.setFont(juce::Font(50.0f, juce::Font::bold));
        juce::ColourGradient grad(
            juce::Colours::hotpink, getWidth() / 2.0f, getHeight() / 3.0f,
            juce::Colours::cyan, getWidth() / 2.0f, getHeight() / 3.0f + 100,
            true);
        g.setGradientFill(grad);
        g.drawText("VocalNite", titleArea, juce::Justification::centred, true);
    }
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    // Flash burst — quick radial bloom from the title centre
    if (flashActive)
    {
        const juce::uint32 elapsed = juce::Time::getMillisecondCounter() - flashStartTimeMs;
        const juce::uint32 flashDuration = 350;

        if (elapsed < flashDuration)
        {
            const float t = (float)elapsed / (float)flashDuration;
            const float alpha = (1.0f - t) * 0.7f;
            const float radius = 60.0f + t * 360.0f;
            const float cx = (float)titleHitArea.getCentreX();
            const float cy = (float)titleHitArea.getCentreY();

            juce::ColourGradient burst(
                juce::Colours::white.withAlpha(alpha), cx, cy,
                juce::Colour(255, 100, 200).withAlpha(0.0f), cx + radius, cy,
                true);
            g.setGradientFill(burst);
            g.fillRect(getLocalBounds());
        }
        else
        {
            flashActive = false;
        }
    }

    for (const auto& s : sparkles)
        drawSparkle(g, s);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto titleArea = area.removeFromTop(200);
    titleLabel.setBounds(titleArea);
    titleHitArea = titleArea;   // keep in sync so mouseDown works before first paint

    auto buttonArea = area.removeFromTop(50).reduced(100, 0);
    signupButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(10));
    loginButton.setBounds(buttonArea.reduced(10));

    verifyButton.setBounds(10, 10, 120, 24);
}

void MainComponent::timerCallback()
{
    starSystem.update(0.0f, getHeight());
    glowLines.update();

    const float dt = 1.0f / 60.0f;

    for (int i = sparkles.size() - 1; i >= 0; --i)
    {
        auto& s = sparkles.getReference(i);
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        s.vy += 220.0f * dt;   // gravity
        s.vx *= 0.985f;         // drag
        s.vy *= 0.985f;
        s.age += dt;

        if (s.age >= s.lifetime)
            sparkles.remove(i);
    }

    // Drive logo fade-in (~400 ms)
    if (showLogo && logoAlpha < 1.0f)
        logoAlpha = juce::jmin(1.0f, logoAlpha + dt / 0.4f);

    repaint();
}

//==============================================================================
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
                if (result != 1) return;

                const juce::String username = dialog->getTextEditorContents("username");
                const juce::String password = dialog->getTextEditorContents("password");

                if (username.isEmpty() || password.isEmpty())
                {
                    DBG("Username or password is empty");
                    return;
                }

                if (type == "Sign Up")
                {
                    const juce::String email = dialog->getTextEditorContents("email");
                    if (email.isEmpty())
                    {
                        DBG("Email is empty");
                        return;
                    }

                    // Any .edu address gets an educational account; all others are normal.
                    const juce::String domain = email.fromLastOccurrenceOf("@", false, false);
                    const juce::String userType = domain.endsWithIgnoreCase(".edu")
                        ? "educational" : "normal";

                    const bool success = DatabaseManager::get().signUp(username, email, password, userType);
                    if (success)
                    {
                        if (userType == "educational")
                        {
                            verifyButton.setVisible(true);
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::InfoIcon,
                                "Account Created!",
                                "A verification token has been sent to " + email
                                + ".\n\nPlease check your email and use the "
                                "Verify Account button to verify your account "
                                "before logging in.");
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
                    const bool success = DatabaseManager::get().login(username, password);
                    if (success)
                    {
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
            }),
        true);
}

//==============================================================================
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
                if (result != 1) return;

                const juce::String token = dialog->getTextEditorContents("token").trim();
                if (token.isEmpty()) return;

                if (DatabaseManager::get().verifyEmail(token))
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
            }),
        true);
}

//==============================================================================
//  Hover effects
//==============================================================================
void MainComponent::mouseEnter(const juce::MouseEvent& event)
{
    if (event.eventComponent == &signupButton) signupButton.setAlpha(0.8f);
    else if (event.eventComponent == &loginButton)  loginButton.setAlpha(0.8f);
}

void MainComponent::mouseExit(const juce::MouseEvent& event)
{
    if (event.eventComponent == &signupButton) signupButton.setAlpha(1.0f);
    else if (event.eventComponent == &loginButton)  loginButton.setAlpha(1.0f);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    // Only react to direct clicks on this component, not events bubbling up
    // from child buttons through the registered mouse listeners.
    if (event.eventComponent != this) return;
    if (showLogo)                      return;   // already swapped
    if (!logoImage.isValid())          return;   // no logo to show

    if (titleHitArea.contains(event.getPosition()))
    {
        showLogo = true;
        triggerSwapAnimation();
        repaint();
    }
}