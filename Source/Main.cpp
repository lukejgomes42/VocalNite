#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
// Standalone MainWindow class
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name,
            juce::Colours::black, // background color
            0)                     // no buttons
    {
        setUsingNativeTitleBar(false);      // hide OS title bar
        setContentOwned(new MainComponent(), true);

        // Fixed starting size
        int startWidth = 600;
        int startHeight = 400;

        // Prevent resizing completely
        setResizable(false, false);

        // Center window on main display
        auto screenArea = juce::Desktop::getInstance().getDisplays().getMainDisplay().userArea;

        // Clamp size to screen if needed
        if (startWidth > screenArea.getWidth())  startWidth = screenArea.getWidth();
        if (startHeight > screenArea.getHeight()) startHeight = screenArea.getHeight();

        setBounds((screenArea.getWidth() - startWidth) / 2,
            (screenArea.getHeight() - startHeight) / 2,
            startWidth,
            startHeight);



        setVisible(true);
    }

    // Prevent user from resizing by forcing size
    void resized() override
    {
        auto r = getBounds();
        r.setSize(600, 400);  // force fixed size
        setBounds(r);
        DocumentWindow::resized();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

//==============================================================================
// Main JUCE application class
class HelloWorldApplication : public juce::JUCEApplication
{
public:
    HelloWorldApplication() {}

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
START_JUCE_APPLICATION(HelloWorldApplication)
