#include "Project.h"
#include "../Database/DatabaseManager.h"

bool Project::createNew(const juce::File& folder, const juce::String& projectName)
{
    if (!folder.exists())
        folder.createDirectory();

    name = projectName;
    projectFile = folder.getChildFile("project.vnite");

    // Save to database and get project ID
    try
    {
        SQLite::Statement query(DatabaseManager::get().db(),
            "INSERT INTO Projects (user_id, name, bpm, time_signature) VALUES (1, ?, 120, '4/4')");
        query.bind(1, name.toStdString());
        query.exec();
        projectId = (int)DatabaseManager::get().db().getLastInsertRowid();
        DBG("Project created with ID: " + juce::String(projectId));
    }
    catch (const std::exception& e)
    {
        DBG("Project DB error: " + juce::String(e.what()));
    }

    return save();
}

bool Project::load(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    auto json = juce::JSON::parse(file);

    if (!json.isObject())
        return false;

    auto* obj = json.getDynamicObject();
    name = obj->getProperty("name").toString();
    projectFile = file;
    projectId = (int)obj->getProperty("projectId");

    return true;
}

bool Project::save()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name", name);
    obj->setProperty("version", 1);
    obj->setProperty("projectId", projectId);

    juce::var json(obj.get());

    return projectFile.replaceWithText(juce::JSON::toString(json));
}