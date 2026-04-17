#pragma once
#include <JuceHeader.h>
#include <libpq-fe.h>

class DatabaseManager
{
public:
    static DatabaseManager& get()
    {
        static DatabaseManager instance;
        return instance;
    }

    PGconn* db() { return pgConnection; }

    bool signUp(const juce::String& username, const juce::String& email, const juce::String& password, const juce::String& userType);
    bool userExists(const juce::String& username);
    bool emailExists(const juce::String& email);
    bool login(const juce::String& username, const juce::String& password);
    int getUserId(const juce::String& username);

    void testPostgresConnection();

    bool sendVerificationEmail(const juce::String& email, const juce::String& token);
    bool verifyEmail(const juce::String& token);
    juce::String generateToken();

    juce::String getLastLoginError() const { return lastLoginError; }

private:
    DatabaseManager();
    ~DatabaseManager();

    PGconn* pgConnection = nullptr;
    juce::String lastLoginError;

    static const std::string CONNECTION_STRING;
};