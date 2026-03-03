#include <JuceHeader.h>
#include "MainComponent.h"
#include "../UI/ProjectManagerComponent.h"
#include "../DAW/DAWComponent.h"

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
        auto* pm = new ProjectManagerComponent();
        pm->setUsername(username);
        pm->onLogout = [this]()
            {
                showLoginScreen();
            };
        pm->onOpenProject = [this](const juce::String& projectName)
            {
                showDAWComponent(projectName);
            };
        setContentOwned(pm, true);
    }

    void showDAWComponent(const juce::String& projectName)
    {
        setResizeLimits(1280, 720, 1280, 720);
        auto* daw = new DAWComponent(projectName);
        setContentOwned(daw, true);
        centreWithSize(1280, 720);
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