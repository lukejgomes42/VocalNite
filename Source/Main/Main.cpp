#include <JuceHeader.h>
#include "MainComponent.h"
#include "../UI/ProjectManagerComponent.h"
#include "../DAW/DAWComponent.h"

//==============================================================================
//  ProjectLoadingScreen
//  Shown briefly while the DAWComponent is being constructed. Sized at the
//  DAW's final dimensions so when the window resizes to match the user sees
//  our styled gradient rather than stretched ProjectManager pixels.
//  Animates pulsing hotpink dots to match the app's overall look.
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
    float        animPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingScreen)
};

//==============================================================================
//  loadAppIcon
//  Walks up from the .exe to find the Resources folder and loads icon.png.
//  Uses the same parent-directory walk as DAWComponent's resource resolution
//  so paths match in both dev (Builds/...) and shipped layouts.
//  Returns an invalid Image if no icon file is found — the caller falls back
//  to the JUCE default.
//==============================================================================
static juce::Image loadAppIcon()
{
    juce::File searchDir = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();

    for (int i = 0; i < 10; ++i)
    {
        juce::File candidate = searchDir.getChildFile("Resources");
        if (candidate.isDirectory())
        {
            juce::File iconFile = candidate.getChildFile("icon.png");
            if (iconFile.existsAsFile())
                return juce::ImageFileFormat::loadFrom(iconFile);
        }
        searchDir = searchDir.getParentDirectory();
    }
    return {};
}

//==============================================================================
//  MainWindow
//  The top-level DocumentWindow that acts as the application's screen router.
//  Transitions: Login -> ProjectManager -> DAW (and back via callbacks).
//  The only persistent state across transitions is currentUsername.
//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name,
            juce::Colours::black,
            DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setResizeLimits(600, 400, 600, 400);
        setResizable(false, false);

        // Custom title-bar icon (top-left corner of the window frame).
        // The OS taskbar icon comes from the .exe's embedded resource, which
        // is set via the Projucer "Icon (large)" field.
        if (auto img = loadAppIcon(); img.isValid())
            setIcon(img);

        const auto screenArea = juce::Desktop::getInstance()
            .getDisplays().getMainDisplay().userArea;

        const int startW = juce::jmin(600, screenArea.getWidth());
        const int startH = juce::jmin(400, screenArea.getHeight());
        setBounds((screenArea.getWidth() - startW) / 2,
            (screenArea.getHeight() - startH) / 2,
            startW, startH);

        showLoginScreen();
        setVisible(true);
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
                showProjectManager(username);
            };
        setContentOwned(login, true);
    }

    void showProjectManager(const juce::String& username)
    {
        if (username.isNotEmpty())
            currentUsername = username;

        // Re-lock to the small dashboard size. setResizable must come BEFORE
        // setResizeLimits — calling setResizable(false,...) after a resizable
        // session can leave a stale corner-resize affordance otherwise.
        setResizable(false, false);
        setResizeLimits(600, 400, 600, 400);
        centreWithSize(600, 400);

        auto* pm = new ProjectManagerComponent();
        pm->setUsername(currentUsername);
        pm->onLogout = [this]() { showLoginScreen(); };
        pm->onOpenProject = [this](const juce::String& name, int id) { showDAWComponent(name, id); };
        setContentOwned(pm, true);
    }

    void showDAWComponent(const juce::String& projectName, int projectId)
    {
        // IMPORTANT: projectName arrives by const-reference from a string
        // array owned by ProjectManagerComponent. The moment setContentOwned
        // is called with the new content, the old component (and its strings)
        // are destroyed — making projectName a dangling reference.
        // Take deep local copies FIRST, then use only those copies.
        const juce::String pname = projectName;
        const int          pid = projectId;

        // setResizable + setResizeLimits must be applied before any
        // setSize/centreWithSize call.  JUCE clamps geometry against the
        // current limits, so the wrong order would silently shrink the window.
        setResizable(true, true);
        setResizeLimits(1280, 720, 32768, 32768);

        // Default the DAW to the work area of whichever display the window
        // currently sits on (screen minus the taskbar).
        const auto workArea = juce::Desktop::getInstance()
            .getDisplays()
            .getDisplayForRect(getBounds())
            ->userArea;

        const int finalW = std::max(workArea.getWidth(), 1280);
        const int finalH = std::max(workArea.getHeight(), 720);

        // Resize FIRST so the loading screen fills the final dimensions.
        // Pass false to setContentOwned so it does not shrink the window back
        // to the component's intrinsic size.
        setBounds(workArea.getX(), workArea.getY(), finalW, finalH);

        auto* loading = new ProjectLoadingScreen(pname);
        loading->setSize(finalW, finalH);
        setContentOwned(loading, false);   // ProjectManager is deleted here

        // Defer DAW construction so the loading screen gets at least one
        // paint frame before the synchronous DB + UI setup begins.
        juce::Timer::callAfterDelay(60, [this, pname, pid]()
            {
                auto* daw = new DAWComponent(pname, pid, currentUsername);
                daw->onReturnToDashboard = [this]() { showProjectManager(""); };
                setContentOwned(daw, false);   // keep window at work-area size
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

//==============================================================================
//  VocalNite  —  JUCEApplication entry point
//==============================================================================
class VocalNite : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(VocalNite)