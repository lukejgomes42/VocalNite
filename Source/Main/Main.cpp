#include <JuceHeader.h>
#include "MainComponent.h"
#include "../UI/ProjectManagerComponent.h"
#include "../DAW/DAWComponent.h"

//==============================================================================
//  ProjectLoadingScreen
//  Shown briefly while the DAWComponent is being constructed. Sized at the
//  DAW's final dimensions (1280x720) so when the window resizes to match,
//  the user sees our styled gradient rather than stretched ProjectManager
//  pixels. Animates pulsing hotpink dots to match the app's overall look.
//==============================================================================
class ProjectLoadingScreen : public juce::Component, private juce::Timer
{
public:
    explicit ProjectLoadingScreen(const juce::String& projectName)
        : projName(projectName)
    {
        startTimerHz(30);
    }

    ~ProjectLoadingScreen() override
    {
        // Stop the timer before any base-class destructor runs. Without this
        // a pending callback could fire on a partially-destroyed object and
        // dereference garbage (manifesting as an atomic-on-invalid-pointer).
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        // Full-screen gradient backdrop (dark purple to near-black)
        juce::ColourGradient bg(juce::Colour(18, 14, 32), 0.0f, 0.0f,
            juce::Colour(10, 8, 22), 0.0f, (float)getHeight(), false);
        g.setGradientFill(bg);
        g.fillAll();

        // Centered card
        const int cardW = 480, cardH = 220;
        juce::Rectangle<int> card((getWidth() - cardW) / 2,
            (getHeight() - cardH) / 2,
            cardW, cardH);

        juce::ColourGradient cg(juce::Colour(40, 28, 64),
            (float)card.getCentreX(), (float)card.getY(),
            juce::Colour(25, 18, 45),
            (float)card.getCentreX(), (float)card.getBottom(), false);
        g.setGradientFill(cg);
        g.fillRoundedRectangle(card.toFloat(), 14.0f);

        g.setColour(juce::Colours::hotpink.withAlpha(0.55f));
        g.drawRoundedRectangle(card.toFloat(), 14.0f, 1.5f);

        // Title
        auto area = card;
        g.setColour(juce::Colours::hotpink);
        g.setFont(juce::Font(24.0f, juce::Font::bold));
        g.drawText("Opening Project", area.removeFromTop(70).reduced(20, 18),
            juce::Justification::centredBottom);

        // Project name (italic, truncated if too long)
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::italic));
        g.drawText(projName, area.removeFromTop(40).reduced(24, 0),
            juce::Justification::centred, true);

        // Pulsing hotpink dots
        const float dotY = card.getBottom() - 48.0f;
        const float centre = (float)card.getCentreX();
        const float spacing = 24.0f;
        for (int i = 0; i < 3; ++i)
        {
            float phase = animPhase + i * 0.33f;
            if (phase > 1.0f) phase -= 1.0f;
            float alpha = 0.25f + 0.75f * std::abs(std::sin(phase * juce::MathConstants<float>::pi));
            g.setColour(juce::Colours::hotpink.withAlpha(alpha));
            g.fillEllipse(centre + (i - 1) * spacing - 5.0f, dotY, 10.0f, 10.0f);
        }
    }

private:
    void timerCallback() override
    {
        animPhase += 0.04f;
        if (animPhase > 1.0f) animPhase -= 1.0f;
        repaint();
    }

    juce::String projName;
    float animPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingScreen)
};

//==============================================================================
// Standalone MainWindow class
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name,
            juce::Colours::black, // background color
            DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizeLimits(600, 400, 600, 400);
        setResizable(false, false);

        // Center and size code stays the same...
        auto screenArea = juce::Desktop::getInstance().getDisplays().getMainDisplay().userArea;
        int startWidth = 600;
        int startHeight = 400;
        if (startWidth > screenArea.getWidth()) startWidth = screenArea.getWidth();
        if (startHeight > screenArea.getHeight()) startHeight = screenArea.getHeight();
        setBounds((screenArea.getWidth() - startWidth) / 2,
            (screenArea.getHeight() - startHeight) / 2,
            startWidth,
            startHeight);

        // <<< Replace the old setContentOwned >>> 
        showLoginScreen();

        setVisible(true);
    }

    // Prevent user from resizing by forcing size
    void resized() override
    {
        DocumentWindow::resized();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    juce::String currentUsername;

    void showLoginScreen()
    {
        auto* login = new MainComponent();

        login->onAuthenticationSuccess = [this](const juce::String& username)
            {
                showProjectManager(username); // pass the logged-in username
            };

        setContentOwned(login, true);
    }

    void showProjectManager(const juce::String& username)
    {
        if (username.isNotEmpty())
            currentUsername = username;

        setResizeLimits(600, 400, 600, 400);
        centreWithSize(600, 400);
        auto* pm = new ProjectManagerComponent();
        pm->setUsername(currentUsername);
        pm->onLogout = [this]()
            {
                showLoginScreen();
            };
        pm->onOpenProject = [this](const juce::String& projectName, int projectId)
            {
                showDAWComponent(projectName, projectId);
            };
        setContentOwned(pm, true);
    }

    void showDAWComponent(const juce::String& projectName, int projectId)
    {
        // ⚠ CRITICAL: projectName arrives by const-reference from
        // ProjectManagerComponent::recentProjectNames[row]. The moment we call
        // setContentOwned(newContent, true), the old ProjectManagerComponent
        // is deleted, taking its string array with it — and `projectName`
        // becomes a dangling reference. Any later copy of it (including the
        // implicit [projectName] capture in our deferred lambda below) would
        // invoke juce::String's refcount-increment on freed memory, crashing
        // inside std::atomic::operator++ with 0xFFFFFFFFFFFFFFFF.
        //
        // Take deep local copies FIRST, before any setContentOwned call, then
        // use only those copies for the rest of this function.
        const juce::String pname = projectName;
        const int          pid = projectId;

        // Put a styled transitional loading screen up at the FINAL window size
        // before building the DAW. The window resize (600x400 -> 1280x720) then
        // reveals our gradient card rather than stretched ProjectManager pixels.
        //
        // After the loading screen paints at least once, we build the DAW on
        // the next message-loop tick. The DAW constructor itself is synchronous
        // (it runs several DB queries + UI setup before kicking off the
        // background voice-bank loader), so the user sees the loading screen
        // animating until the DAW takes over.

        setResizeLimits(1280, 720, 1280, 720);

        auto* loading = new ProjectLoadingScreen(pname);
        loading->setSize(1280, 720);
        setContentOwned(loading, true);   // <-- ProjectManagerComponent dies here
        centreWithSize(1280, 720);

        // Defer DAW construction so the loading screen gets a chance to paint.
        // 60ms is a comfortable margin on the message thread — enough for a
        // couple of frames without feeling laggy. Captures are `this` + our
        // own local string/int copies (safe to outlive the original PM refs).
        // The MainWindow is owned by the JUCEApplication's unique_ptr and is
        // guaranteed to outlive any ~60ms delay during normal operation.
        juce::Timer::callAfterDelay(60,
            [this, pname, pid]()
            {
                auto* daw = new DAWComponent(pname, pid, currentUsername);
                daw->onReturnToDashboard = [this]() { showProjectManager(""); };
                setContentOwned(daw, true);
            });
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

//==============================================================================
// Main JUCE application class
class VocalNite : public juce::JUCEApplication
{
public:
    VocalNite() {}

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr; // deletes our window
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(VocalNite)