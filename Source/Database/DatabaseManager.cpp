#include <JuceHeader.h>
#include "DatabaseManager.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>

DatabaseManager::DatabaseManager()
{
    juce::File dbFile = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("VocalNite/vocalnitedb.db");

    dbFile.getParentDirectory().createDirectory();

    juce::File appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File templateDb = appFile.getParentDirectory()
        .getChildFile("Resources/database/vocalnitedb.db");

    if (!dbFile.existsAsFile())
    {
        if (templateDb.existsAsFile())
            templateDb.copyFileTo(dbFile);
    }

    try
    {
        database = std::make_unique<SQLite::Database>(
            dbFile.getFullPathName().toStdString(),
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE
        );
        initializeSchema();
    }
    catch (const std::exception& e)
    {
        DBG("Database error: " << e.what());
    }
}

void DatabaseManager::testDB()
{
    try
    {
        SQLite::Statement query(*database, "SELECT name FROM sqlite_master WHERE type='table'");
        while (query.executeStep())
        {
            DBG("Table: " + juce::String(query.getColumn(0).getString()));
        }
    }
    catch (std::exception& e)
    {
        DBG("SQLite error: " + juce::String(e.what()));
    }
}

bool DatabaseManager::userExists(const juce::String& username)
{
    try
    {
        SQLite::Statement query(*database, "SELECT COUNT(*) FROM Users WHERE username = ?");
        query.bind(1, username.toStdString());
        if (query.executeStep())
            return query.getColumn(0).getInt() > 0;
    }
    catch (const std::exception& e)
    {
        DBG("userExists error: " + juce::String(e.what()));
    }
    return false;
}

//signup function
bool DatabaseManager::signUp(const juce::String& username, const juce::String& password, const juce::String& userType)
{
    if (userExists(username))
    {
        DBG("Signup failed: username already exists");
        return false;
    }

    // Generate a random salt
    juce::String salt = juce::Uuid().toString();

    // Hash the password + salt using SHA-256
    juce::String saltedPassword = password + salt;
    juce::SHA256 sha256(saltedPassword.toUTF8(), saltedPassword.getNumBytesAsUTF8());
    juce::String passwordHash = sha256.toHexString();

    try
    {
        SQLite::Statement query(*database,
            "INSERT INTO Users (username, password_hash, salt, user_type) VALUES (?, ?, ?, ?)");
        query.bind(1, username.toStdString());
        query.bind(2, passwordHash.toStdString());
        query.bind(3, salt.toStdString());
        query.bind(4, userType.toStdString());
        query.exec();
        DBG("Signup successful for user: " + username);
        return true;
    }
    catch (const std::exception& e)
    {
        DBG("Signup error: " + juce::String(e.what()));
        return false;
    }
}

//login function
bool DatabaseManager::login(const juce::String& username, const juce::String& password)
{
    try
    {
        SQLite::Statement query(*database, "SELECT password_hash, salt FROM Users WHERE username = ?");
        query.bind(1, username.toStdString());

        if (query.executeStep())
        {
            juce::String storedHash = juce::String(query.getColumn(0).getString());
            juce::String salt = juce::String(query.getColumn(1).getString());

            // Hash the provided password with the stored salt
            juce::String saltedPassword = password + salt;
            juce::SHA256 sha256(saltedPassword.toUTF8(), saltedPassword.getNumBytesAsUTF8());
            juce::String passwordHash = sha256.toHexString();

            if (passwordHash == storedHash)
            {
                DBG("Login successful for user: " + username);
                return true;
            }
            else
            {
                DBG("Login failed: incorrect password");
                return false;
            }
        }
        else
        {
            DBG("Login failed: user not found");
            return false;
        }
    }
    catch (const std::exception& e)
    {
        DBG("Login error: " + juce::String(e.what()));
        return false;
    }
}

void DatabaseManager::initializeSchema()
{
    try
    {
        database->exec(
            "CREATE TABLE IF NOT EXISTS Patterns ("
            "    pattern_id  INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    project_id  INTEGER NOT NULL,"
            "    name        TEXT NOT NULL,"
            "    FOREIGN KEY (project_id) REFERENCES Projects(project_id) ON DELETE CASCADE"
            ");"
        );

        database->exec(
            "CREATE TABLE IF NOT EXISTS PatternNotes ("
            "    note_id     INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    pattern_id  INTEGER NOT NULL,"
            "    pitch       INTEGER NOT NULL,"
            "    beat        INTEGER NOT NULL,"
            "    FOREIGN KEY (pattern_id) REFERENCES Patterns(pattern_id) ON DELETE CASCADE"
            ");"
        );

        // Add pattern_id column to VocalClips if it doesn't exist
        try
        {
            database->exec("ALTER TABLE VocalClips ADD COLUMN pattern_id INTEGER REFERENCES Patterns(pattern_id);");
        }
        catch (...) {} // Column may already exist, ignore error

        // Add lyric column to PatternNotes if it doesn't exist
        try
        {
            database->exec("ALTER TABLE PatternNotes ADD COLUMN lyric TEXT DEFAULT '';");
        }
        catch (...) {} // Column may already exist, ignore error

        DBG("Schema initialized successfully");
    }
    catch (const std::exception& e)
    {
        DBG("Schema initialization error: " + juce::String(e.what()));
    }
}