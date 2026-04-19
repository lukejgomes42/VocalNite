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

        label.setText("Project Name", juce::dontSendNotification);
        label.setFont(juce::Font(13.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white);

        nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(30, 10, 50));
        nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(100, 40, 140));
        nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::hotpink);

        okButton.setButtonText("Create");
        okButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 0, 90));
        okButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        cancelButton.setButtonText("Cancel");
        cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colour(40, 40, 60));
        cancelButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

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

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(15, 15, 25));
        g.setColour(juce::Colour(60, 40, 90));
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        label.setBounds(r.removeFromTop(20));
        r.removeFromTop(6);
        nameEditor.setBounds(r.removeFromTop(28));
        r.removeFromTop(12);
        auto buttons = r.removeFromTop(32);
        okButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(4));
        cancelButton.setBounds(buttons.reduced(4));
    }

private:
    juce::Label label;
    juce::TextEditor nameEditor;
    juce::TextButton okButton, cancelButton;
    std::function<void(const juce::String&)> callback;
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

    createButton.setButtonText("+ New Project");
    createButton.setColour(juce::TextButton::buttonColourId, juce::Colour(60, 0, 90));
    createButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(90, 0, 130));
    createButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(createButton);

    openButton.setButtonText("Open Project");
    openButton.setColour(juce::TextButton::buttonColourId, juce::Colour(30, 10, 50));
    openButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(50, 20, 80));
    openButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(openButton);

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::hotpink);
    statusLabel.setFont(juce::Font(12.0f));
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    eduModeToggle.setToggleState(
        EducationalModeManager::getInstance().isEnabled(),
        juce::dontSendNotification);

    eduModeToggle.setColour(juce::ToggleButton::textColourId,
        juce::Colour(180, 140, 210));

    eduModeToggle.onClick = [this]()
        {
            bool newState = eduModeToggle.getToggleState();
            EducationalModeManager::getInstance().setEnabled(newState);

            eduModeToggle.setColour(juce::ToggleButton::textColourId,
                newState ? juce::Colours::cyan
                : juce::Colour(180, 140, 210));
        };

    addAndMakeVisible(eduModeToggle);

    // Recent projects list
    recentProjectsList.setColour(juce::ListBox::backgroundColourId, juce::Colour(20, 10, 35));
    recentProjectsList.setColour(juce::ListBox::outlineColourId, juce::Colour(60, 40, 90));
    recentProjectsList.setOutlineThickness(1);
    recentProjectsList.setRowHeight(36);
    recentProjectsList.setModel(this);
    addAndMakeVisible(recentProjectsList);

    // CREATE
    createButton.onClick = [this]()
        {
            auto modal = new CreateProjectModal([this](const juce::String& projectName)
                {
                    if (projectName.isEmpty())
                    {
                        statusLabel.setText("Project name cannot be empty", juce::dontSendNotification);
                        return;
                    }

                    juce::File projectsFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("VocalNite")
                        .getChildFile("Projects");

                    projectsFolder.createDirectory();

                    juce::File projectFolder = projectsFolder.getChildFile(projectName);
                    if (!projectFolder.createDirectory())
                    {
                        statusLabel.setText("Failed to create folder", juce::dontSendNotification);
                        return;
                    }

                    if (currentProject.createNew(projectFolder, projectName))
                    {
                        statusLabel.setText("", juce::dontSendNotification);
                        refreshRecentProjects();
                        if (onOpenProject)
                            onOpenProject(currentProject.getName(), currentProject.getProjectId());
                    }
                });

            modal->setSize(300, 140);
            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned(modal);
            options.dialogTitle = "New Project";
            options.dialogBackgroundColour = juce::Colour(15, 15, 25);
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = false;
            options.resizable = false;
            options.launchAsync();
        };

    // OPEN
    openButton.onClick = [this]()
        {
            juce::File projectsFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("VocalNite")
                .getChildFile("Projects");

            if (!projectsFolder.exists())
            {
                statusLabel.setText("No projects found", juce::dontSendNotification);
                return;
            }

            auto files = projectsFolder.findChildFiles(juce::File::findFiles, true, "*.vnite");

            if (files.isEmpty())
            {
                statusLabel.setText("No projects to open", juce::dontSendNotification);
                return;
            }

            // Just open the file chooser inline using a popup menu
            juce::PopupMenu menu;
            for (int i = 0; i < files.size(); ++i)
                menu.addItem(i + 1, files[i].getParentDirectory().getFileName());

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(openButton),
                [this, files](int result)
                {
                    if (result > 0)
                    {
                        if (currentProject.load(files[result - 1]))
                        {
                            statusLabel.setText("", juce::dontSendNotification);
                            if (onOpenProject)
                                onOpenProject(currentProject.getName(), currentProject.getProjectId());
                        }
                    }
                });
        };

    refreshRecentProjects();
}

void ProjectManagerComponent::refreshRecentProjects()
{
    recentFiles.clear();

    juce::File projectsFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("VocalNite")
        .getChildFile("Projects");

    if (projectsFolder.exists())
        recentFiles = projectsFolder.findChildFiles(juce::File::findFiles, true, "*.vnite");

    recentProjectsList.updateContent();
    recentProjectsList.repaint();
}

// ListBoxModel
int ProjectManagerComponent::getNumRows()
{
    return recentFiles.isEmpty() ? 1 : recentFiles.size(); // 1 for empty message
}

void ProjectManagerComponent::paintListBoxItem(int rowNumber, juce::Graphics& g,
    int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.setColour(juce::Colour(60, 20, 90));
        g.fillRect(0, 0, width, height);
    }

    if (recentFiles.isEmpty())
    {
        g.setColour(juce::Colour(100, 80, 120));
        g.setFont(juce::Font(13.0f));
        g.drawText("No recent projects", 0, 0, width, height, juce::Justification::centred);
        return;
    }

    // Project icon dot
    g.setColour(juce::Colours::hotpink.withAlpha(0.8f));
    g.fillEllipse(14, height / 2 - 4, 8, 8);

    // Project name
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(13.0f));
    juce::String name = recentFiles[rowNumber].getParentDirectory().getFileName();
    g.drawText(name, 32, 0, width - 48, height, juce::Justification::centredLeft);

    // Separator line
    g.setColour(juce::Colour(40, 25, 60));
    g.drawLine(12, height - 1, width - 12, height - 1, 1.0f);
}

void ProjectManagerComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (recentFiles.isEmpty() || row >= recentFiles.size()) return;

    if (currentProject.load(recentFiles[row]))
    {
        statusLabel.setText("", juce::dontSendNotification);
        if (onOpenProject)
            onOpenProject(currentProject.getName(), currentProject.getProjectId());
    }
}

void ProjectManagerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(15, 15, 25));

    // Subtle dot grid
    g.setColour(juce::Colour(40, 20, 60).withAlpha(0.4f));
    for (int x = 0; x < getWidth(); x += 24)
        for (int y = 50; y < getHeight(); y += 24)
            g.fillRect(x, y, 1, 1);

    // Card background
    auto card = getCentreCardBounds();
    g.setColour(juce::Colour(20, 10, 35));
    g.fillRoundedRectangle(card.toFloat(), 12.0f);
    g.setColour(juce::Colour(60, 40, 90));
    g.drawRoundedRectangle(card.toFloat(), 12.0f, 1.0f);

    // "Recent Projects" header inside card
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(150, 100, 180));
    g.drawText("RECENT PROJECTS",
        card.getX() + 16, card.getY() + 12,
        card.getWidth() - 32, 20,
        juce::Justification::centredLeft);

    // Divider below header
    g.setColour(juce::Colour(60, 40, 90));
    g.drawLine(card.getX() + 16, card.getY() + 36,
        card.getRight() - 16, card.getY() + 36, 1.0f);
}

void ProjectManagerComponent::resized()
{
    if (getWidth() == 0 || getHeight() == 0) return;

    auto area = getLocalBounds();
    topBar.setBounds(area.removeFromTop(50));

    auto card = getCentreCardBounds();

    // List box fills most of the card, below the header
    int listTop = card.getY() + 42;
    int btnH = 38;
    int btnY = card.getBottom() - btnH - 12;
    int listBottom = btnY - 10;

    recentProjectsList.setBounds(card.getX() + 12, listTop,
        card.getWidth() - 24, listBottom - listTop);

    // Buttons side by side at bottom of card
    int btnW = (card.getWidth() - 48) / 2;
    createButton.setBounds(card.getX() + 16, btnY, btnW, btnH);
    openButton.setBounds(card.getX() + 16 + btnW + 16, btnY, btnW, btnH);

    statusLabel.setBounds(card.getX(), card.getBottom() + 6, card.getWidth(), 18);

    eduModeToggle.setBounds(
        card.getX() + 16,
        card.getBottom() + 10,
        200, 24
    );
}

juce::Rectangle<int> ProjectManagerComponent::getCentreCardBounds() const
{
    int cardW = getWidth() > 80 ? std::min(500, getWidth() - 80) : 500;
    int cardH = getHeight() > 120 ? std::min(380, getHeight() - 120) : 380;
    int cardX = (getWidth() - cardW) / 2;
    int cardY = 50 + (getHeight() - 50 - cardH) / 2;
    return { cardX, cardY, cardW, cardH };
}