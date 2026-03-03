#include "Project.h"

bool Project::createNew(const juce::File& folder, const juce::String& projectName)
{
    if (!folder.exists())
        folder.createDirectory();

    name = projectName;
    projectFile = folder.getChildFile("project.vnite");

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

    return true;
}

bool Project::save()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name", name);
    obj->setProperty("version", 1);

    juce::var json(obj.get());

    return projectFile.replaceWithText(juce::JSON::toString(json));
}