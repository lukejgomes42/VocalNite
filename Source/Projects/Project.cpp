#include <JuceHeader.h>
#include "Project.h"
#include "../Database/DatabaseManager.h"
#include <libpq-fe.h>

bool Project::createNew(const juce::File& folder, const juce::String& projectName, int userId)
{
    if (!folder.exists())
        folder.createDirectory();

    name = projectName;
    projectFile = folder.getChildFile("project.vnite");

    // Save to database and get project ID
    std::string userIdStr = std::to_string(userId);
    const char* params[2] = { projectName.toRawUTF8(), userIdStr.c_str() };
    PGresult* res = PQexecParams(DatabaseManager::get().db(),
        "INSERT INTO Projects (user_id, name, bpm, time_signature) VALUES ($2, $1, 120, '4/4') RETURNING project_id",
        2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        projectId = std::stoi(PQgetvalue(res, 0, 0));
        DBG("Project created with ID: " + juce::String(projectId));
    }
    else
    {
        DBG("Project DB error: " + juce::String(PQerrorMessage(DatabaseManager::get().db())));
    }
    PQclear(res);

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