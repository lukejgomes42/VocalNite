#include "ProjectManagerComponent.h"
#include "../Projects/Project.h"

// =========================
// Modal component for creating a project
// =========================
class CreateProjectModal : public juce::Component
{
public:
    CreateProjectModal(std::function<void(const juce::String&)> cb)
        : callback(cb)
    {
        addAndMakeVisible(label);
        addAndMakeVisible(nameEditor);
        addAndMakeVisible(okButton);
        addAndMakeVisible(cancelButton);

        label.setText("Enter project name:", juce::dontSendNotification);

        okButton.setButtonText("Create");
        cancelButton.setButtonText("Cancel");

        okButton.onClick = [this]()
            {
                if (callback)
                    callback(nameEditor.getText().trim());

                if (auto* top = getTopLevelComponent())
                    top->exitModalState(0);
            };

        cancelButton.onClick = [this]()
            {
                if (auto* top = getTopLevelComponent())
                    top->exitModalState(0);
            };
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        label.setBounds(r.removeFromTop(20));
        nameEditor.setBounds(r.removeFromTop(25));
        auto buttons = r.removeFromTop(35);
        okButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(5));
        cancelButton.setBounds(buttons.reduced(5));
    }

private:
    juce::Label label;
    juce::TextEditor nameEditor;
    juce::TextButton okButton, cancelButton;
    std::function<void(const juce::String&)> callback;
};

// =========================
// Modal component for selecting a project to open
// =========================
class OpenProjectModal : public juce::Component
{
public:
    OpenProjectModal(const juce::Array<juce::File>& projectFiles,
        std::function<void(const juce::File&)> cb)
        : files(projectFiles), callback(cb)
    {
        addAndMakeVisible(combo);
        addAndMakeVisible(openButton);
        addAndMakeVisible(cancelButton);

        for (auto& f : files)
            combo.addItem(f.getParentDirectory().getFileName(), combo.getNumItems() + 1);

        openButton.setButtonText("Open");
        cancelButton.setButtonText("Cancel");

        openButton.onClick = [this]()
            {
                int index = combo.getSelectedItemIndex();
                if (index >= 0 && callback)
                    callback(files[index]);

                if (auto* top = getTopLevelComponent())
                    top->exitModalState(0);
            };

        cancelButton.onClick = [this]()
            {
                if (auto* top = getTopLevelComponent())
                    top->exitModalState(0);
            };
    }

    void resized() override
    {
        combo.setBounds(20, 20, getWidth() - 40, 25);
        openButton.setBounds(20, 60, (getWidth() - 60) / 2, 30);
        cancelButton.setBounds(openButton.getRight() + 20, 60, (getWidth() - 60) / 2, 30);
    }

private:
    juce::ComboBox combo;
    juce::TextButton openButton, cancelButton;
    juce::Array<juce::File> files;
    std::function<void(const juce::File&)> callback;
};

// =========================
// ProjectManagerComponent Implementation
// =========================
ProjectManagerComponent::ProjectManagerComponent()
{
    topBar.onLogoutClicked = [this]()
        {
            if (onLogout)
                onLogout();
        };

    addAndMakeVisible(topBar);
    addAndMakeVisible(createButton);
    addAndMakeVisible(openButton);

    placeholderLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    placeholderLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholderLabel);

    // CREATE PROJECT
    createButton.onClick = [this]()
        {
            auto modal = new CreateProjectModal([this](const juce::String& projectName)
                {
                    if (projectName.isEmpty())
                    {
                        placeholderLabel.setText("Project name cannot be empty", juce::dontSendNotification);
                        return;
                    }

                    // Use Documents/VocalNite/Projects
                    juce::File projectsFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("VocalNite")
                        .getChildFile("Projects");

                    projectsFolder.createDirectory();

                    juce::File projectFolder = projectsFolder.getChildFile(projectName);
                    if (!projectFolder.createDirectory())
                    {
                        placeholderLabel.setText("Failed to create folder", juce::dontSendNotification);
                        return;
                    }

                    if (currentProject.createNew(projectFolder, projectName))
                    {
                        placeholderLabel.setText("Created: " + currentProject.getName(),
                            juce::dontSendNotification);
                    }
                });

            modal->setSize(300, 150);

            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned(modal);
            options.dialogTitle = "New Project";
            options.dialogBackgroundColour = juce::Colours::darkgrey;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            options.launchAsync();
        };

    // OPEN PROJECT
    openButton.onClick = [this]()
        {
            juce::File projectsFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("VocalNite")
                .getChildFile("Projects");

            if (!projectsFolder.exists())
            {
                placeholderLabel.setText("No projects found", juce::dontSendNotification);
                return;
            }

            auto files = projectsFolder.findChildFiles(juce::File::findFiles, true, "*.vnite");

            if (files.isEmpty())
            {
                placeholderLabel.setText("No projects to open", juce::dontSendNotification);
                return;
            }

            auto modal = new OpenProjectModal(files, [this](const juce::File& f)
                {
                    if (currentProject.load(f))
                        placeholderLabel.setText("Opened: " + currentProject.getName(),
                            juce::dontSendNotification);
                });

            modal->setSize(400, 130);

            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned(modal);
            options.dialogTitle = "Open Project";
            options.dialogBackgroundColour = juce::Colours::darkgrey;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            options.launchAsync();
        };
}

void ProjectManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void ProjectManagerComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds(area.removeFromTop(50));

    auto buttonArea = area.removeFromBottom(100).reduced(50, 10);
    createButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(10));
    openButton.setBounds(buttonArea.reduced(10));

    placeholderLabel.setBounds(area.reduced(50, 0));
}