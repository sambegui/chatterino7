#include "providers/kick/KickAccountManager.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickOAuthFlow.hpp"
#include "singletons/Settings.hpp"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace chatterino {

KickAccountManager::KickAccountManager()
{
}

std::shared_ptr<KickAccount> KickAccountManager::getCurrent()
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->currentUser_;
}

QString KickAccountManager::getCurrentUsername() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->currentUser_)
    {
        return this->currentUser_->getUserName();
    }
    return QString();
}

bool KickAccountManager::isLoggedIn() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->currentUser_ != nullptr &&
           this->currentUser_->isAuthenticated();
}

void KickAccountManager::startLogin()
{
    if (this->oauthFlow_ && this->oauthFlow_->isInProgress())
    {
        // A previous attempt never got its callback (browser tab closed, Kick
        // errored out before redirecting, ...). Clicking login again means the
        // user wants to retry, so drop the stale flow instead of ignoring them.
        qCDebug(chatterinoKick)
            << "Discarding an OAuth flow that never completed";
        this->oauthFlow_->cancel();
    }

    this->oauthFlow_ = std::make_unique<KickOAuthFlow>();

    std::ignore = this->oauthFlow_->authenticationSuccess.connect(
        [this](const KickOAuthFlow::Tokens &tokens) {
            this->onOAuthSuccess(tokens);
        });

    std::ignore = this->oauthFlow_->authenticationFailed.connect(
        [this](const QString &error) {
            this->onOAuthFailed(error);
        });

    if (!this->oauthFlow_->start())
    {
        qCWarning(chatterinoKick) << "Failed to start OAuth flow";
    }
}

void KickAccountManager::logout()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);

        if (this->currentUser_)
        {
            KickAccount::clearSavedCredentials();
            this->currentUser_.reset();
            qCDebug(chatterinoKick) << "User logged out";
        }
    }

    // Clear accounts (must be done outside of lock since it accesses GUI thread)
    while (!this->accounts.empty())
    {
        this->accounts.removeAt(0);
    }

    // Notify listeners without holding the lock
    this->currentUserChanged();
}

void KickAccountManager::load()
{
    auto account = KickAccount::loadFromSettings();
    if (account)
    {
        this->setCurrentUser(account);
        this->accounts.append(account);
    }
}

void KickAccountManager::reloadUsers()
{
    // Reload account from settings
    auto account = KickAccount::loadFromSettings();
    if (account)
    {
        this->setCurrentUser(account);
    }
}

void KickAccountManager::setCurrentUser(std::shared_ptr<KickAccount> account)
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->currentUser_ = account;
    }

    // Connect to authentication change signals to handle token refresh failures
    if (account)
    {
        std::ignore =
            account->authenticationChanged.connect([this](bool authenticated) {
                if (!authenticated)
                {
                    qCWarning(chatterinoKick)
                        << "Authentication expired. Please log in again.";
                    this->authenticationExpired.invoke();
                }
            });
    }

    this->currentUserChanged();
}

void KickAccountManager::onOAuthSuccess(const KickOAuthFlow::Tokens &tokens)
{
    qCDebug(chatterinoKick) << "OAuth succeeded, fetching user info...";

    // Store tokens temporarily while we fetch user info
    this->pendingTokens_ = tokens;

    // Try to decode user info from JWT token
    QString username = this->extractUsernameFromToken(tokens.accessToken);

    if (!username.isEmpty())
    {
        qCDebug(chatterinoKick) << "Got username from token:" << username;
        this->createAccountWithUsername(username);
    }
    else
    {
        // Fallback: use token introspection to get user info
        this->fetchUserInfoFromApi(tokens.accessToken);
    }
}

QString KickAccountManager::extractUsernameFromToken(const QString &accessToken)
{
    // JWT tokens are in format: header.payload.signature
    // The payload is base64url encoded JSON
    QStringList parts = accessToken.split('.');
    if (parts.size() != 3)
    {
        qCDebug(chatterinoKick) << "Token is not a valid JWT format";
        return QString();
    }

    // Decode the payload (second part)
    QByteArray payload = QByteArray::fromBase64(parts[1].toUtf8(),
                                                QByteArray::Base64UrlEncoding);

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        qCDebug(chatterinoKick)
            << "Failed to parse JWT payload:" << error.errorString();
        return QString();
    }

    QJsonObject obj = doc.object();

    // Try common claim names for username
    if (obj.contains("preferred_username"))
    {
        return obj["preferred_username"].toString();
    }
    if (obj.contains("username"))
    {
        return obj["username"].toString();
    }
    if (obj.contains("name"))
    {
        return obj["name"].toString();
    }
    if (obj.contains("sub"))
    {
        // 'sub' might be the user ID or username
        QString sub = obj["sub"].toString();
        // If it looks like a username (not just numeric), use it
        bool isNumeric = false;
        sub.toLongLong(&isNumeric);
        if (!isNumeric)
        {
            return sub;
        }
        qCDebug(chatterinoKick)
            << "JWT 'sub' claim is numeric (user ID):" << sub;
    }

    qCDebug(chatterinoKick) << "No username found in JWT token";
    return QString();
}

void KickAccountManager::fetchUserInfoFromApi(const QString &accessToken)
{
    qCDebug(chatterinoKick) << "Fetching user info from Kick API...";

    auto *manager = new QNetworkAccessManager();

    // Official Kick API endpoint (from docs.kick.com/apis/users):
    // GET https://api.kick.com/public/v1/users
    // "If no user IDs are specified, the information for the currently
    // authorised user will be returned by default."
    // Response: { "data": [{ "user_id": 12345, "name": "username", ... }] }
    QUrl usersUrl{"https://api.kick.com/public/v1/users"};
    QNetworkRequest request{usersUrl};
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(accessToken).toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, [this, reply, manager, accessToken]() {
            reply->deleteLater();

            QByteArray responseData = reply->readAll();
            int statusCode =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            qCDebug(chatterinoKick)
                << "GET /users response:" << statusCode << responseData;

            if (reply->error() == QNetworkReply::NoError && statusCode == 200)
            {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                QString username = this->extractUsernameFromResponse(doc);
                if (!username.isEmpty())
                {
                    manager->deleteLater();
                    qCDebug(chatterinoKick)
                        << "Got username from GET /users:" << username;
                    this->createAccountWithUsername(username);
                    return;
                }
            }

            // Try alternative endpoints as fallback
            qCDebug(chatterinoKick)
                << "GET /users failed, trying token introspect as fallback...";
            this->fetchUserInfoFromIntrospect(accessToken, manager);
        });
}

QString KickAccountManager::extractUsernameFromResponse(
    const QJsonDocument &doc)
{
    if (!doc.isObject())
    {
        return QString();
    }

    QJsonObject obj = doc.object();

    // Check for direct username fields (various possible names)
    if (obj.contains("username"))
    {
        return obj["username"].toString();
    }
    if (obj.contains("preferred_username"))
    {
        return obj["preferred_username"].toString();
    }
    if (obj.contains("name"))
    {
        return obj["name"].toString();
    }
    if (obj.contains("slug"))
    {
        return obj["slug"].toString();
    }

    // Check for data wrapper with array (common Kick API pattern)
    if (obj.contains("data"))
    {
        QJsonValue dataVal = obj["data"];

        // data could be an array of users
        if (dataVal.isArray())
        {
            QJsonArray arr = dataVal.toArray();
            if (!arr.isEmpty())
            {
                QJsonObject user = arr[0].toObject();
                // Official Kick API returns "name" field for username
                if (user.contains("name"))
                {
                    return user["name"].toString();
                }
                if (user.contains("username"))
                {
                    return user["username"].toString();
                }
                if (user.contains("slug"))
                {
                    return user["slug"].toString();
                }
            }
        }
        // data could be a single user object
        else if (dataVal.isObject())
        {
            QJsonObject dataObj = dataVal.toObject();
            if (dataObj.contains("username"))
            {
                return dataObj["username"].toString();
            }
            if (dataObj.contains("name"))
            {
                return dataObj["name"].toString();
            }
            if (dataObj.contains("slug"))
            {
                return dataObj["slug"].toString();
            }
        }
    }

    // Check for users array (alternative format)
    if (obj.contains("users") && obj["users"].isArray())
    {
        QJsonArray arr = obj["users"].toArray();
        if (!arr.isEmpty())
        {
            QJsonObject user = arr[0].toObject();
            QString name = user["name"].toString();
            if (!name.isEmpty())
            {
                return name;
            }
            return user["username"].toString();
        }
    }

    return QString();
}

void KickAccountManager::fetchUserInfoFromIntrospect(
    const QString &accessToken, QNetworkAccessManager *manager)
{
    // Official Kick API endpoint (from docs.kick.com/apis/users):
    // POST https://api.kick.com/public/v1/token/introspect
    // Token is passed via Authorization header, not query string
    // Returns: { "data": { "active": true, "client_id": "...", ... } }
    // Note: This endpoint may not return username, but let's try it
    QUrl url{"https://api.kick.com/public/v1/token/introspect"};
    QNetworkRequest request{url};
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(accessToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = manager->post(request, QByteArray());

    QObject::connect(
        reply, &QNetworkReply::finished, [this, reply, manager, accessToken]() {
            reply->deleteLater();

            QByteArray responseData = reply->readAll();
            qCDebug(chatterinoKick) << "Introspect response:" << responseData;

            QString username;

            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                if (doc.isObject())
                {
                    QJsonObject obj = doc.object();

                    // Handle data wrapper
                    QJsonObject dataObj = obj;
                    if (obj.contains("data") && obj["data"].isObject())
                    {
                        dataObj = obj["data"].toObject();
                    }

                    // Try various fields that might contain username
                    if (dataObj.contains("username"))
                    {
                        username = dataObj["username"].toString();
                    }
                    else if (dataObj.contains("preferred_username"))
                    {
                        username = dataObj["preferred_username"].toString();
                    }
                    else if (dataObj.contains("name"))
                    {
                        username = dataObj["name"].toString();
                    }
                    else if (dataObj.contains("sub"))
                    {
                        // sub is typically user ID, but might be username
                        QString sub = dataObj["sub"].toString();
                        bool isNumeric = false;
                        sub.toLongLong(&isNumeric);
                        if (!isNumeric && !sub.isEmpty())
                        {
                            username = sub;
                        }
                        else
                        {
                            qCDebug(chatterinoKick)
                                << "Introspect 'sub' is numeric (user ID):"
                                << sub;
                        }
                    }
                }
            }

            if (username.isEmpty())
            {
                // Last resort: try /api/v1/current-user (unofficial)
                qCDebug(chatterinoKick)
                    << "No username from introspect, trying current-user...";
                this->fetchCurrentUser(accessToken, manager);
                return;
            }

            manager->deleteLater();
            qCDebug(chatterinoKick)
                << "Got username from introspect:" << username;
            this->createAccountWithUsername(username);
        });
}

void KickAccountManager::fetchCurrentUser(const QString &accessToken,
                                          QNetworkAccessManager *manager)
{
    // Try unofficial /api/v1/current-user endpoint (may exist on Kick)
    QUrl url{"https://kick.com/api/v1/user"};
    QNetworkRequest request{url};
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(accessToken).toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, [this, reply, manager, accessToken]() {
            reply->deleteLater();

            QByteArray responseData = reply->readAll();
            qCDebug(chatterinoKick)
                << "Current user response:"
                << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                << responseData;

            QString username;

            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                username = this->extractUsernameFromResponse(doc);
            }

            if (username.isEmpty())
            {
                // Try one more endpoint - the v2 API
                qCDebug(chatterinoKick) << "Trying v2 user endpoint...";
                this->fetchUserV2(accessToken, manager);
                return;
            }

            manager->deleteLater();
            qCDebug(chatterinoKick)
                << "Got username from current-user:" << username;
            this->createAccountWithUsername(username);
        });
}

void KickAccountManager::fetchUserV2(const QString &accessToken,
                                     QNetworkAccessManager *manager)
{
    // Try v2 API which might return current user info
    QUrl url{"https://kick.com/api/v2/user"};
    QNetworkRequest request{url};
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(accessToken).toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, [this, reply, manager]() {
        reply->deleteLater();
        manager->deleteLater();

        QByteArray responseData = reply->readAll();
        qCDebug(chatterinoKick)
            << "V2 user response:"
            << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
            << responseData;

        QString username;

        if (reply->error() == QNetworkReply::NoError)
        {
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            username = this->extractUsernameFromResponse(doc);
        }

        if (username.isEmpty())
        {
            // Kick API doesn't provide a way to get the current user's username
            // Prompt the user to enter it manually
            qCDebug(chatterinoKick)
                << "Could not determine username from API, prompting user...";
            this->promptForUsername();
            return;
        }

        qCDebug(chatterinoKick) << "Final username:" << username;
        this->createAccountWithUsername(username);
    });
}

void KickAccountManager::promptForUsername()
{
    // Run on main thread since we need to show a dialog
    QMetaObject::invokeMethod(
        qApp,
        [this]() {
            bool ok = false;
            QString username = QInputDialog::getText(
                nullptr, "Kick Username Required",
                "The Kick API doesn't provide your username.\n"
                "Please enter your Kick username:",
                QLineEdit::Normal, QString(), &ok);

            if (ok && !username.isEmpty())
            {
                username = username.trimmed().toLower();
                qCDebug(chatterinoKick) << "User entered username:" << username;
                this->createAccountWithUsername(username);
            }
            else
            {
                qCWarning(chatterinoKick)
                    << "User cancelled username prompt, using placeholder";
                this->createAccountWithUsername("kick_user");
            }
        },
        Qt::QueuedConnection);
}

void KickAccountManager::createAccountWithUsername(const QString &username)
{
    auto account = std::make_shared<KickAccount>(
        username, this->pendingTokens_.accessToken,
        this->pendingTokens_.refreshToken, this->pendingTokens_.expiresAt);

    account->saveToSettings();
    this->setCurrentUser(account);
    this->accounts.append(account);

    this->loginSucceeded.invoke(account);
    qCDebug(chatterinoKick) << "Kick login successful as:" << username;

    // Clear pending tokens
    this->pendingTokens_ = KickOAuthFlow::Tokens();
}

void KickAccountManager::onOAuthFailed(const QString &error)
{
    qCWarning(chatterinoKick) << "OAuth failed:" << error;
    this->loginFailed.invoke(error);
}

}  // namespace chatterino
