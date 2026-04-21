#include "ProjectManagerComponent.h"
#include "../Projects/Project.h"
#include "../Database/DatabaseManager.h"
#include <libpq-fe.h>

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

    // Recent projects list
    recentProjectsList.setColour(juce::ListBox::backgroundColourId, juce::Colour(20, 10, 35));
    recentProjectsList.setColour(juce::ListBox::outlineColourId, juce::Colour(60, 40, 90));
    recentProjectsList.setOutlineThickness(1);
    recentProjectsList.setRowHeight(36);
    recentProjectsList.setModel(this);
    addAndMakeVisible(recentProjectsList);

    // Educational Mode toggle (hidden by default; shown only for verified edu users)
    educationalModeToggle.setButtonText("Educational Mode");
    educationalModeToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::cyan);
    educationalModeToggle.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    educationalModeToggle.setColour(juce::ToggleButton::tickDisabledColourId,
        juce::Colour(60, 60, 80));
    educationalModeToggle.setToggleState(EducationalModeManager::getInstance().isEnabled(),
        juce::dontSendNotification);
    educationalModeToggle.onClick = [this]()
        {
            bool newState = educationalModeToggle.getToggleState();
            EducationalModeManager::getInstance().setEnabled(newState);
        };
    educationalModeToggle.setVisible(false);
    addAndMakeVisible(educationalModeToggle);

    educationalModeHint.setFont(juce::Font(11.0f, juce::Font::italic));
    educationalModeHint.setColour(juce::Label::textColourId, juce::Colour(120, 120, 150));
    educationalModeHint.setJustificationType(juce::Justification::centred);
    educationalModeHint.setText("Tooltips, synthesis inspector, and visual hints",
        juce::dontSendNotification);
    educationalModeHint.setVisible(false);
    addAndMakeVisible(educationalModeHint);

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

                    int userId = DatabaseManager::get().getUserId(currentUsername);
                    if (currentProject.createNew(projectFolder, projectName, userId))
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
            if (recentProjectNames.isEmpty())
            {
                statusLabel.setText("No projects to open", juce::dontSendNotification);
                return;
            }

            juce::PopupMenu menu;
            for (int i = 0; i < recentProjectNames.size(); ++i)
                menu.addItem(i + 1, recentProjectNames[i]);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(openButton),
                [this](int result)
                {
                    if (result > 0)
                    {
                        statusLabel.setText("", juce::dontSendNotification);
                        if (onOpenProject)
                            onOpenProject(recentProjectNames[result - 1], recentProjectIds[result - 1]);
                    }
                });
        };

    refreshRecentProjects();
}

void ProjectManagerComponent::refreshRecentProjects()
{
    recentFiles.clear();
    recentProjectNames.clear();
    recentProjectIds.clear();

    int userId = DatabaseManager::get().getUserId(currentUsername);
    if (userId < 0) return;

    std::string userIdStr = std::to_string(userId);
    const char* params[1] = { userIdStr.c_str() };

    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "SELECT project_id, name FROM Projects WHERE user_id = $1 ORDER BY project_id DESC",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i)
        {
            recentProjectIds.add(std::stoi(PQgetvalue(res, i, 0)));
            recentProjectNames.add(juce::String(PQgetvalue(res, i, 1)));
        }
    }
    PQclear(res);

    recentProjectsList.updateContent();
    recentProjectsList.repaint();
}

// ListBoxModel
int ProjectManagerComponent::getNumRows()
{
    return recentProjectNames.isEmpty() ? 1 : recentProjectNames.size();
}

void ProjectManagerComponent::paintListBoxItem(int rowNumber, juce::Graphics& g,
    int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.setColour(juce::Colour(60, 20, 90));
        g.fillRect(0, 0, width, height);
    }

    if (recentProjectNames.isEmpty())
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
    g.drawText(recentProjectNames[rowNumber], 32, 0, width - 48, height, juce::Justification::centredLeft);

    // Separator line
    g.setColour(juce::Colour(40, 25, 60));
    g.drawLine(12, height - 1, width - 12, height - 1, 1.0f);
}

void ProjectManagerComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (recentProjectNames.isEmpty() || row >= recentProjectNames.size()) return;

    if (onOpenProject)
        onOpenProject(recentProjectNames[row], recentProjectIds[row]);
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

    // Reserve space for educational toggle + hint if visible
    bool showEdu = educationalModeToggle.isVisible();
    int eduBlockH = showEdu ? 48 : 0;   // toggle row + hint line

    int btnY = card.getBottom() - btnH - 12 - eduBlockH;
    int listBottom = btnY - 10;

    recentProjectsList.setBounds(card.getX() + 12, listTop,
        card.getWidth() - 24, listBottom - listTop);

    // Buttons side by side at bottom of card
    int btnW = (card.getWidth() - 48) / 2;
    createButton.setBounds(card.getX() + 16, btnY, btnW, btnH);
    openButton.setBounds(card.getX() + 16 + btnW + 16, btnY, btnW, btnH);

    if (showEdu)
    {
        int toggleY = btnY + btnH + 8;
        educationalModeToggle.setBounds(card.getX() + 16, toggleY,
            card.getWidth() - 32, 22);
        educationalModeHint.setBounds(card.getX() + 16, toggleY + 22,
            card.getWidth() - 32, 16);
    }

    statusLabel.setBounds(card.getX(), card.getBottom() + 6, card.getWidth(), 18);
}

juce::Rectangle<int> ProjectManagerComponent::getCentreCardBounds() const
{
    int cardW = getWidth() > 80 ? std::min(500, getWidth() - 80) : 500;
    int cardH = getHeight() > 120 ? std::min(380, getHeight() - 120) : 380;
    int cardX = (getWidth() - cardW) / 2;
    int cardY = 50 + (getHeight() - 50 - cardH) / 2;
    return { cardX, cardY, cardW, cardH };
}

void ProjectManagerComponent::setUsername(const juce::String& name)
{
    topBar.setUsername(name);
    currentUsername = name;
    currentUserType = DatabaseManager::get().getUserType(name);
    applyUserType();
    refreshRecentProjects();
}

void ProjectManagerComponent::applyUserType()
{
    bool isEducational = (currentUserType == "educational");

    // Toggle only exists for educational users
    educationalModeToggle.setVisible(isEducational);
    educationalModeHint.setVisible(isEducational);

    if (!isEducational)
    {
        // A non-edu user must never have ed-mode on (e.g. if they logged out
        // from an edu session in the same app run, force it off).
        if (EducationalModeManager::getInstance().isEnabled())
            EducationalModeManager::getInstance().setEnabled(false);
        educationalModeToggle.setToggleState(false, juce::dontSendNotification);
    }
    else
    {
        // Sync the toggle to whatever the manager's state currently is
        educationalModeToggle.setToggleState(
            EducationalModeManager::getInstance().isEnabled(),
            juce::dontSendNotification);
    }

    resized();
    repaint();
}

ProjectManagerComponent::~ProjectManagerComponent()
{
    // Turn ed-mode off when this screen is destroyed (i.e. user signs out
    // or opens a project). Keeps state sane across screen transitions.
    // DAWComponent re-reads the manager state on its own construction.
}