#include <JuceHeader.h>
#include "DatabaseManager.h"
#include <libpq-fe.h>
#include <curl/curl.h>

struct EmailPayload
{
    std::string data;
    size_t pos = 0;
};

static size_t curlReadCallback(char* ptr, size_t size, size_t nmemb, void* userp)
{
    auto* payload = static_cast<EmailPayload*>(userp);
    size_t available = payload->data.size() - payload->pos;
    size_t toWrite = (size * nmemb < available) ? size * nmemb : available;    if (toWrite == 0) return 0;
    memcpy(ptr, payload->data.c_str() + payload->pos, toWrite);
    payload->pos += toWrite;
    return toWrite;
}

const std::string DatabaseManager::CONNECTION_STRING =
#PUT THE CONNECTION STRING HERE!!!!!!!

DatabaseManager::DatabaseManager()
{
    pgConnection = PQconnectdb(CONNECTION_STRING.c_str());

    if (PQstatus(pgConnection) != CONNECTION_OK)
    {
        DBG("PostgreSQL connection failed: " + juce::String(PQerrorMessage(pgConnection)));
        PQfinish(pgConnection);
        pgConnection = nullptr;
    }
    else
    {
        DBG("PostgreSQL connection successful!");
    }
}

DatabaseManager::~DatabaseManager()
{
    if (pgConnection != nullptr)
    {
        PQfinish(pgConnection);
        pgConnection = nullptr;
    }
}

void DatabaseManager::testPostgresConnection()
{
    if (pgConnection && PQstatus(pgConnection) == CONNECTION_OK)
        DBG("PostgreSQL is connected!");
    else
        DBG("PostgreSQL is NOT connected!");
}

bool DatabaseManager::userExists(const juce::String& username)
{
    if (!pgConnection) return false;

    std::string query = "SELECT COUNT(*) FROM Users WHERE username = $1";
    const char* params[1] = { username.toRawUTF8() };

    PGresult* result = PQexecParams(pgConnection, query.c_str(), 1, nullptr, params, nullptr, nullptr, 0);

    bool exists = false;
    if (PQresultStatus(result) == PGRES_TUPLES_OK)
        exists = std::stoi(PQgetvalue(result, 0, 0)) > 0;
    else
        DBG("userExists error: " + juce::String(PQerrorMessage(pgConnection)));

    PQclear(result);
    return exists;
}

bool DatabaseManager::emailExists(const juce::String& email)
{
    if (!pgConnection) return false;

    std::string query = "SELECT COUNT(*) FROM Users WHERE email = $1";
    const char* params[1] = { email.toRawUTF8() };

    PGresult* result = PQexecParams(pgConnection, query.c_str(), 1, nullptr, params, nullptr, nullptr, 0);

    bool exists = false;
    if (PQresultStatus(result) == PGRES_TUPLES_OK)
        exists = std::stoi(PQgetvalue(result, 0, 0)) > 0;
    else
        DBG("emailExists error: " + juce::String(PQerrorMessage(pgConnection)));

    PQclear(result);
    return exists;
}

bool DatabaseManager::signUp(const juce::String& username, const juce::String& email, const juce::String& password, const juce::String& userType)
{
    if (!pgConnection) return false;

    if (userExists(username))
    {
        DBG("Signup failed: username already exists");
        return false;
    }

    if (emailExists(email))
    {
        DBG("Signup failed: email already exists");
        return false;
    }

    // Generate salt and hash password
    juce::String salt = juce::Uuid().toString();
    juce::String saltedPassword = password + salt;
    juce::SHA256 sha256(saltedPassword.toUTF8(), saltedPassword.getNumBytesAsUTF8());
    juce::String passwordHash = sha256.toHexString();

    juce::String token = generateToken();

    std::string query = "INSERT INTO Users (username, email, password_hash, salt, user_type, verification_token, email_verified) VALUES ($1, $2, $3, $4, $5, $6, FALSE)";
    const char* params[6] = {
        username.toRawUTF8(),
        email.toRawUTF8(),
        passwordHash.toRawUTF8(),
        salt.toRawUTF8(),
        userType.toRawUTF8(),
        token.toRawUTF8()
    };

    PGresult* result = PQexecParams(pgConnection, query.c_str(), 6, nullptr, params, nullptr, nullptr, 0);

    bool success = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!success)
        DBG("Signup error: " + juce::String(PQerrorMessage(pgConnection)));
    else
        DBG("Signup successful for: " + username);
        if (userType == "educational")
            sendVerificationEmail(email, token);

    PQclear(result);
    return success;
}

bool DatabaseManager::login(const juce::String& username, const juce::String& password)
{
    lastLoginError = "";
    if (!pgConnection) return false;

    std::string query = "SELECT password_hash, salt, email_verified, user_type FROM Users WHERE username = $1";
    const char* params[1] = { username.toRawUTF8() };

    PGresult* result = PQexecParams(pgConnection, query.c_str(), 1, nullptr, params, nullptr, nullptr, 0);

    bool success = false;
    if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0)
    {
        juce::String storedHash = juce::String(PQgetvalue(result, 0, 0));
        juce::String salt = juce::String(PQgetvalue(result, 0, 1));

        juce::String saltedPassword = password + salt;
        juce::SHA256 sha256(saltedPassword.toUTF8(), saltedPassword.getNumBytesAsUTF8());
        juce::String passwordHash = sha256.toHexString();

        juce::String userType = juce::String(PQgetvalue(result, 0, 3));
        bool emailVerified = juce::String(PQgetvalue(result, 0, 2)) == "t";
        if (userType == "educational" && !emailVerified)
        {
            DBG("Login failed: email not verified");
            lastLoginError = "Please verify your email before logging in.";
            success = false;
        }
        else
        {
            success = (passwordHash == storedHash);
            if (success)
            {
                lastLoginError = "";
                DBG("Login successful!");
            }
            else
            {
                lastLoginError = "Incorrect username or password.";
                DBG("Login failed: incorrect password");
            }
        }
    }
    else
    {
        DBG("Login failed: user not found");
        lastLoginError = "Incorrect username or password.";
    }

    PQclear(result);
    return success;
}

int DatabaseManager::getUserId(const juce::String& username)
{
    if (!pgConnection) return -1;

    std::string query = "SELECT user_id FROM Users WHERE username = $1";
    const char* params[1] = { username.toRawUTF8() };

    PGresult* result = PQexecParams(pgConnection, query.c_str(), 1, nullptr, params, nullptr, nullptr, 0);

    int userId = -1;
    if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0)
        userId = std::stoi(PQgetvalue(result, 0, 0));

    PQclear(result);
    return userId;
}

bool DatabaseManager::sendVerificationEmail(const juce::String& email, const juce::String& token)
{
    const std::string gmailUser = "vocalnite3@gmail.com";
	#PUT THE GMAIL APP PASSWORD HERE!!!!!!!!

    const std::string emailBody =
        "From: VocalNite <" + gmailUser + ">\r\n"
        "To: " + email.toStdString() + "\r\n"
        "Subject: Verify your VocalNite account\r\n"
        "\r\n"
        "Welcome to VocalNite!\r\n\r\n"
        "Your verification token is:\r\n\r\n"
        + token.toStdString() + "\r\n\r\n"
        "Enter this token in the application to verify your account.\r\n"
        "If you did not create an account, please ignore this email.\r\n";

    EmailPayload payload{ emailBody, 0 };

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        DBG("Failed to initialize curl");
        return false;
    }

    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, email.toRawUTF8());

    std::string fromStr = "<" + gmailUser + ">";

    curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
    curl_easy_setopt(curl, CURLOPT_USERNAME, gmailUser.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, gmailPassword.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, fromStr.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, curlReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        DBG("Email send failed: " + juce::String(curl_easy_strerror(res)));
        return false;
    }

    DBG("Verification email sent to: " + email);
    return true;
}

juce::String DatabaseManager::generateToken()
{
    return juce::Uuid().toString().removeCharacters("-");
}

bool DatabaseManager::verifyEmail(const juce::String& token)
{
    if (!pgConnection) return false;

    const char* params[1] = { token.toRawUTF8() };
    PGresult* result = PQexecParams(pgConnection,
        "UPDATE Users SET email_verified = TRUE, verification_token = NULL WHERE verification_token = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    bool success = PQresultStatus(result) == PGRES_COMMAND_OK && PQcmdTuples(result) != std::string("0");
    PQclear(result);
    return success;
}