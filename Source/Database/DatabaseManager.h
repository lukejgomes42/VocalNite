#pragma once
#include <JuceHeader.h>
#include <libpq-fe.h>

// =============================================================================
//  DatabaseManager
//
//  Singleton wrapper around a libpq PostgreSQL connection.
//  Access via DatabaseManager::get(). Every caller builds its own
//  PQexecParams queries with $1/$2 placeholders using the raw PGconn*
//  returned by db().
//
//  Connection credentials are read from Secrets.h (not committed — see
//  Secrets.h.template for setup instructions).
// =============================================================================
class DatabaseManager
{
public:
    static DatabaseManager& get()
    {
        static DatabaseManager instance;
        return instance;
    }

    // Raw connection handle — callers build their own parameterised queries.
    PGconn* db() { return pgConnection; }

    // ── Authentication ───────────────────────────────────────────────────────
    bool         signUp(const juce::String& username,
        const juce::String& email,
        const juce::String& password,
        const juce::String& userType);
    bool         userExists(const juce::String& username);
    bool         emailExists(const juce::String& email);
    bool         login(const juce::String& username, const juce::String& password);
    int          getUserId(const juce::String& username);

    // Returns "educational" only when user_type=educational AND email_verified=true.
    // Falls back to "normal" for unverified educational accounts (safe downgrade).
    juce::String getUserType(const juce::String& username);

    // ── Email verification ───────────────────────────────────────────────────
    bool         sendVerificationEmail(const juce::String& email, const juce::String& token);
    bool         verifyEmail(const juce::String& token);
    juce::String generateToken();

    // ── Diagnostics ─────────────────────────────────────────────────────────
    void testPostgresConnection();
    juce::String getLastLoginError() const { return lastLoginError; }

private:
    DatabaseManager();
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    PGconn* pgConnection = nullptr;
    juce::String lastLoginError;
};