#pragma once
#include <memory>
#include <SQLiteCpp/SQLiteCpp.h>

class DatabaseManager
{
public:
    static DatabaseManager& get()
    {
        static DatabaseManager instance;
        return instance;
    }

    SQLite::Database& db() { return *database; }

    void testDB();  
    bool signUp(const juce::String& username, const juce::String& password, const juce::String& userType);
    bool userExists(const juce::String& username);
    bool login(const juce::String& username, const juce::String& password);

private:
    DatabaseManager();
    std::unique_ptr<SQLite::Database> database;
};
