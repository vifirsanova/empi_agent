#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QTemporaryFile>
#include <QDateTime>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QEventLoop>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QSet>
#include <QMap>
#include <QUuid>
#include <QRegularExpression>
#include <QFileInfo>
#include <QCryptographicHash>
#include <future>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>

#include <jwt-cpp/jwt.h>

#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"
#include "DocumentParser.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// CONFIGURATION
// ============================================================================

struct Config {
    std::string api_key;
    std::string api_base;
    std::string folder_id;
    std::string cloud_model;
    std::string local_model_path;
    std::string jwt_secret;
    std::string access_code_hash;
    int max_attempts = 3;
    int block_duration_seconds = 300;
};

Config load_config(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        qWarning() << "[Config] File not found:" << path.c_str();
        return cfg;
    }
    
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        if (section != "llm" && section != "security") continue;
        
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
        if (!val.empty() && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        
        if (key == "api_key") cfg.api_key = val;
        else if (key == "api_base") cfg.api_base = val;
        else if (key == "folder_id") cfg.folder_id = val;
        else if (key == "model") cfg.cloud_model = val;
        else if (key == "local_model_path") cfg.local_model_path = val;
        else if (key == "jwt_secret") cfg.jwt_secret = val;
        else if (key == "access_code_hash") cfg.access_code_hash = val;
        else if (key == "max_attempts") cfg.max_attempts = std::stoi(val);
        else if (key == "block_duration_seconds") cfg.block_duration_seconds = std::stoi(val);
    }
    
    qDebug() << "[Config] jwt_secret loaded, length:" << cfg.jwt_secret.length();
    qDebug() << "[Config] access_code_hash loaded, length:" << cfg.access_code_hash.length();
    return cfg;
}

// ============================================================================
// JWT HELPERS
// ============================================================================

class JwtAuth {
public:
    JwtAuth(const std::string& secret) : m_secret(secret) {
        qDebug() << "[JwtAuth] Initialized with secret length:" << secret.length();
    }
    
    std::string generateToken(const std::string& userId) {
        auto now = std::chrono::system_clock::now();
        auto token = jwt::create()
            .set_issuer("empi-agent")
            .set_type("JWS")
            .set_id(generateId())
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::hours(24))
            .set_subject(userId)
            .sign(jwt::algorithm::hs256{m_secret});
        return token;
    }
    
    bool validateToken(const std::string& token, std::string& userId) {
        qDebug() << "[JwtAuth] Validating token, length:" << token.length();
        
        if (m_secret.empty()) {
            qWarning() << "[JwtAuth] Secret is empty! Cannot validate.";
            return false;
        }
        
        try {
            auto decoded = jwt::decode(token);
            auto verifier = jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{m_secret})
                .with_issuer("empi-agent");
            verifier.verify(decoded);
            userId = decoded.get_subject();
            qDebug() << "[JwtAuth] Token valid, subject:" << userId.c_str();
            return true;
        } catch (const std::exception& e) {
            qWarning() << "[JwtAuth] Validation failed:" << e.what();
            return false;
        }
    }
    
private:
    std::string generateId() {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    }
    std::string m_secret;
};

// ============================================================================
// AUTH BLOCKER
// ============================================================================

class AuthBlocker {
public:
    AuthBlocker(int maxAttempts, int blockSeconds)
        : m_maxAttempts(maxAttempts), m_blockSeconds(blockSeconds) {}
    
    bool isBlocked(const QString& ip) {
        QMutexLocker locker(&m_mutex);
        auto it = m_blocks.find(ip);
        if (it == m_blocks.end()) return false;
        
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now - it.value().blockedUntil > 0) {
            m_blocks.remove(ip);
            return false;
        }
        return true;
    }
    
    void recordAttempt(const QString& ip, bool success) {
        QMutexLocker locker(&m_mutex);
        
        if (success) {
            m_blocks.remove(ip);
            return;
        }
        
        BlockInfo& block = m_blocks[ip];
        block.attempts++;
        
        if (block.attempts >= m_maxAttempts) {
            block.blockedUntil = QDateTime::currentSecsSinceEpoch() + m_blockSeconds;
            qDebug() << "[AuthBlocker] IP blocked:" << ip << "until" << block.blockedUntil;
        }
    }
    
    int getRemainingAttempts(const QString& ip) {
        QMutexLocker locker(&m_mutex);
        auto it = m_blocks.find(ip);
        if (it == m_blocks.end()) return m_maxAttempts;
        
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now - it.value().blockedUntil > 0) {
            m_blocks.remove(ip);
            return m_maxAttempts;
        }
        
        return m_maxAttempts - it.value().attempts;
    }
    
    int getBlockedUntil(const QString& ip) {
        QMutexLocker locker(&m_mutex);
        auto it = m_blocks.find(ip);
        if (it == m_blocks.end()) return 0;
        return it.value().blockedUntil;
    }

private:
    struct BlockInfo {
        int attempts = 0;
        qint64 blockedUntil = 0;
    };
    
    QMap<QString, BlockInfo> m_blocks;
    QMutex m_mutex;
    int m_maxAttempts;
    int m_blockSeconds;
};

// ============================================================================
// HTTP PARSER
// ============================================================================

struct HttpRequest {
    QString method;
    QString path;
    QString body;
    QMap<QString, QString> headers;
    QMap<QString, QString> queryParams;
};

HttpRequest parseHttpRequest(const QByteArray& data) {
    HttpRequest req;
    QString request = QString::fromUtf8(data);
    QStringList lines = request.split("\r\n");
    
    if (lines.isEmpty()) return req;
    
    QStringList parts = lines[0].split(" ");
    if (parts.size() >= 3) {
        req.method = parts[0];
        req.path = parts[1];
        
        if (req.path.contains("?")) {
            QStringList pathParts = req.path.split("?");
            req.path = pathParts[0];
            QStringList params = pathParts[1].split("&");
            for (const QString& param : params) {
                QStringList kv = param.split("=");
                if (kv.size() == 2) {
                    req.queryParams[kv[0]] = QUrl::fromPercentEncoding(kv[1].toUtf8());
                }
            }
        }
    }
    
    bool bodyStarted = false;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) {
            bodyStarted = true;
            continue;
        }
        if (!bodyStarted) {
            int colon = lines[i].indexOf(":");
            if (colon > 0) {
                QString key = lines[i].left(colon).trimmed();
                QString value = lines[i].mid(colon + 1).trimmed();
                req.headers[key] = value;
            }
        } else {
            req.body += lines[i];
        }
    }
    
    return req;
}

QByteArray buildHttpResponse(int statusCode, const QString& statusText, 
                             const QString& contentType, const QByteArray& body) {
    QString response = QString("HTTP/1.1 %1 %2\r\n")
                       .arg(statusCode)
                       .arg(statusText);
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QString::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response += "\r\n";
    return response.toUtf8() + body;
}

// ============================================================================
// ADAPTATION TASK
// ============================================================================

class AdaptationTask : public QRunnable {
public:
    AdaptationTask(
        const QString& text,
        const QString& prompt,
        std::shared_ptr<EMPI::LLMClient> llm,
        std::shared_ptr<EMPI::TextAnalyzer> textAgent,
        std::shared_ptr<EMPI::FeedbackAgent> feedbackAgent,
        std::shared_ptr<EMPI::InterfaceGenerator> interfaceGen,
        std::function<void(const QString&, const QString&)> callback
    ) : m_text(text)
      , m_prompt(prompt)
      , m_llm(llm)
      , m_textAgent(textAgent)
      , m_feedbackAgent(feedbackAgent)
      , m_interfaceGen(interfaceGen)
      , m_callback(callback)
    {
        setAutoDelete(true);
    }

    void run() override {
        QString taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        qDebug() << "[AdaptationTask] Started, taskId:" << taskId;
        
        try {
            bool isBinary = false;
            for (QChar ch : m_text) {
                if (ch.unicode() == 0 || (ch.unicode() < 32 && ch.unicode() != 10 && 
                    ch.unicode() != 13 && ch.unicode() != 9)) {
                    isBinary = true;
                    break;
                }
            }
            
            QString cleanText = m_text;
            if (isBinary) {
                cleanText = "[Warning: Binary content detected. Please upload PDF, DOCX, or text files for proper parsing.]\n\n" + m_text.left(500);
                qDebug() << "[AdaptationTask] Binary content detected, truncated to 500 chars";
            }
            
            json dialog = json::array();
            if (!m_prompt.isEmpty()) {
                dialog.push_back({{"role", "user"}, {"content", m_prompt.toStdString()}});
                dialog.push_back({{"role", "assistant"}, {"content", "Adapting..."}});
            } else {
                dialog.push_back({{"role", "user"}, {"content", "Please adapt this text."}});
            }
            
            qDebug() << "[AdaptationTask] Starting async analysis for task:" << taskId;
            
            auto f1 = std::async(std::launch::async, [&]() {
                qDebug() << "[AdaptationTask] TextAnalyzer processing...";
                return m_textAgent->process_raw({{"text", cleanText.toStdString()}});
            });
            auto f2 = std::async(std::launch::async, [&]() {
                qDebug() << "[AdaptationTask] FeedbackAgent processing...";
                return m_feedbackAgent->process_raw({{"dialog_history", dialog}});
            });
            
            json m = f1.get()["payload"]["data"];
            json f = f2.get()["payload"]["data"];
            
            qDebug() << "[AdaptationTask] Metrics and feedback received";
            
            json ig = {
                {"text_metrics", m.value("metrics", json::object())},
                {"feedback_analysis", f.value("analysis", json::object())},
                {"original_text", cleanText.toStdString()}
            };
            
            qDebug() << "[AdaptationTask] InterfaceGenerator processing...";
            json r = m_interfaceGen->process_raw(ig);
            QString html = QString::fromStdString(r["payload"]["data"].value("html", ""));
            
            html = stripMarkdownBlocks(html);
            html = sanitizeHtml(html);
            
            qDebug() << "[AdaptationTask] Completed, HTML length:" << html.length();
            m_callback(taskId, html);
            
        } catch (const std::exception& e) {
            qWarning() << "[AdaptationTask] Exception:" << e.what();
            m_callback(taskId, QString("Error: %1").arg(e.what()));
        }
    }

private:
    QString stripMarkdownBlocks(const QString& html) {
        QString result = html;
        QRegularExpression re("^```(?:html)?\\s*\\n?", QRegularExpression::CaseInsensitiveOption);
        result.remove(re);
        result.remove(QRegularExpression("\\n?```\\s*$"));
        return result.trimmed();
    }
    
    QString sanitizeHtml(const QString& html) {
        if (html.isEmpty()) return html;
        
        QString result = html;
        
        QRegularExpression scriptRegex("<script[^>]*>[\\s\\S]*?</script>", 
                                        QRegularExpression::CaseInsensitiveOption);
        result.remove(scriptRegex);
        
        QRegularExpression iframeRegex("<iframe[^>]*>[\\s\\S]*?</iframe>", 
                                       QRegularExpression::CaseInsensitiveOption);
        result.remove(iframeRegex);
        
        QRegularExpression objectRegex("<object[^>]*>[\\s\\S]*?</object>", 
                                       QRegularExpression::CaseInsensitiveOption);
        result.remove(objectRegex);
        
        QRegularExpression embedRegex("<embed[^>]*>", 
                                      QRegularExpression::CaseInsensitiveOption);
        result.remove(embedRegex);
        
        QRegularExpression jsLinkRegex("href\\s*=\\s*[\"']javascript:[^\"']*[\"']", 
                                       QRegularExpression::CaseInsensitiveOption);
        result.replace(jsLinkRegex, "href=\"#\"");
        
        QRegularExpression eventRegex("\\son\\w+\\s*=\\s*[\"'][^\"']*[\"']", 
                                      QRegularExpression::CaseInsensitiveOption);
        result.remove(eventRegex);
        
        return result;
    }
    
    QString m_text;
    QString m_prompt;
    std::shared_ptr<EMPI::LLMClient> m_llm;
    std::shared_ptr<EMPI::TextAnalyzer> m_textAgent;
    std::shared_ptr<EMPI::FeedbackAgent> m_feedbackAgent;
    std::shared_ptr<EMPI::InterfaceGenerator> m_interfaceGen;
    std::function<void(const QString&, const QString&)> m_callback;
};

// ============================================================================
// HTTP SERVER
// ============================================================================

class HttpServer : public QObject {
    Q_OBJECT
public:
    HttpServer(const std::string& configPath, QObject* parent = nullptr) 
        : QObject(parent) {
        
        qDebug() << "[HttpServer] Initializing...";
        Config cfg = load_config(configPath);
        
        if (cfg.jwt_secret.empty()) {
            qWarning() << "[HttpServer] JWT_SECRET IS EMPTY! Authentication will fail.";
        }
        
        m_llm = std::make_shared<EMPI::LLMClient>("python3");
        m_textAgent = std::make_shared<EMPI::TextAnalyzer>();
        m_feedbackAgent = std::make_shared<EMPI::FeedbackAgent>(m_llm);
        m_interfaceGen = std::make_shared<EMPI::InterfaceGenerator>(m_llm, cfg.local_model_path);
        m_docParser = std::make_shared<DocumentParser>();
        m_networkManager = new QNetworkAccessManager(this);
        
        m_jwtAuth = std::make_unique<JwtAuth>(cfg.jwt_secret);
        m_accessCodeHash = QString::fromStdString(cfg.access_code_hash);
        m_blocker = std::make_unique<AuthBlocker>(cfg.max_attempts, cfg.block_duration_seconds);
        
        QThreadPool::globalInstance()->setMaxThreadCount(
            std::max(1, QThread::idealThreadCount())
        );
        
        qDebug() << "[HttpServer] Initialized with" 
                 << QThreadPool::globalInstance()->maxThreadCount() 
                 << "threads";
        qDebug() << "[HttpServer] Auth: max_attempts=" << cfg.max_attempts 
                 << ", block_duration=" << cfg.block_duration_seconds << "s";
    }

    void start(int port = 8080) {
        m_server = new QTcpServer(this);
        
        connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
        
        if (!m_server->listen(QHostAddress::Any, port)) {
            qCritical() << "[HttpServer] Failed to listen on port" << port;
            return;
        }
        
        qDebug() << "========================================";
        qDebug() << "EMPI Agent HTTP Server (Qt5 + JWT + Block)";
        qDebug() << "========================================";
        qDebug() << "Port:" << port;
        qDebug() << "Threads:" << QThreadPool::globalInstance()->maxThreadCount();
        qDebug() << "----------------------------------------";
        qDebug() << "Endpoints:";
        qDebug() << "  POST /api/auth            - Login with code";
        qDebug() << "  GET  /api/health          - Health check (public)";
        qDebug() << "  GET  /api/status          - Server status (auth)";
        qDebug() << "  POST /api/adapt           - Adapt text (auth)";
        qDebug() << "  GET  /api/result/<id>     - Get result (auth)";
        qDebug() << "  POST /api/fetch           - Fetch URL (auth)";
        qDebug() << "  POST /api/parse           - Parse document (auth)";
        qDebug() << "========================================";
        qDebug() << "[HttpServer] Listening on port" << port;
    }

private slots:
    void onNewConnection() {
        QTcpSocket* client = m_server->nextPendingConnection();
        if (!client) return;
        
        qDebug() << "[HttpServer] New connection from:" << client->peerAddress().toString();
        
        connect(client, &QTcpSocket::readyRead, this, [this, client]() {
            handleClient(client);
        });
        
        connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
    }

private:
    QString getClientIP(QTcpSocket* client) {
        return client->peerAddress().toString();
    }
    
    bool extractToken(const HttpRequest& req, QString& token) {
        auto it = req.headers.find("Authorization");
        if (it != req.headers.end()) {
            QString auth = it.value();
            if (auth.startsWith("Bearer ", Qt::CaseInsensitive)) {
                token = auth.mid(7).trimmed();
                qDebug() << "[extractToken] Token extracted, length:" << token.length();
                return !token.isEmpty();
            }
        }
        qDebug() << "[extractToken] No Authorization header found";
        return false;
    }
    
    bool validateRequest(const HttpRequest& req, QString& userId) {
        if (req.path == "/api/health" || req.path == "/api/auth") {
            return true;
        }

        QString token;
        if (!extractToken(req, token)) {
            qDebug() << "[validateRequest] No token for:" << req.path;
            return false;
        }

        std::string userIdStd;
        bool valid = m_jwtAuth->validateToken(token.toStdString(), userIdStd);
        if (!valid) {
            qDebug() << "[validateRequest] Invalid token for:" << req.path;
            return false;
        }
        userId = QString::fromStdString(userIdStd);
        qDebug() << "[validateRequest] Token valid for:" << userId;
        return true;
    }
    
    void handleClient(QTcpSocket* client) {
        QByteArray data = client->readAll();
        if (data.isEmpty()) return;
        
        HttpRequest req = parseHttpRequest(data);
        QString clientIP = getClientIP(client);
        
        qDebug() << "[HttpServer] Request:" << req.method << req.path << "from" << clientIP;
        qDebug() << "[HttpServer] Body length:" << req.body.length();
        
        if (req.method == "OPTIONS") {
            client->write(buildHttpResponse(204, "No Content", "text/plain", QByteArray()));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        if (m_blocker->isBlocked(clientIP)) {
            qDebug() << "[HttpServer] Blocked IP:" << clientIP;
            QJsonObject error;
            error["error"] = "Too many failed attempts";
            error["message"] = "Your IP is blocked. Try again later.";
            error["blocked_until"] = m_blocker->getBlockedUntil(clientIP);
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(429, "Too Many Requests", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        if (req.path == "/api/auth" && req.method == "POST") {
            handleAuth(client, req, clientIP);
        } else if (req.path == "/api/health") {
            handleHealth(client);
        } else if (req.path == "/api/status") {
            QString userId;
            if (!validateRequest(req, userId)) {
                QJsonObject error;
                error["error"] = "Unauthorized";
                QByteArray body = QJsonDocument(error).toJson();
                client->write(buildHttpResponse(401, "Unauthorized", "application/json", body));
                client->flush();
                client->disconnectFromHost();
                return;
            }
            handleStatus(client);
        } else if (req.path == "/api/adapt" && req.method == "POST") {
            handleAdapt(client, req);
        } else if (req.path.startsWith("/api/result/")) {
            QString taskId = req.path.mid(12);
            handleResult(client, taskId);
        } else if (req.path == "/api/fetch" && req.method == "POST") {
            handleFetch(client, req);
        } else if (req.path == "/api/parse" && req.method == "POST") {
            handleParse(client, req);
        } else {
            handleStaticFile(client, req.path);
        }
    }
    
    void handleAuth(QTcpSocket* client, const HttpRequest& req, const QString& clientIP) {
        qDebug() << "[handleAuth] Started from IP:" << clientIP;
        
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            qDebug() << "[handleAuth] Invalid JSON";
            QJsonObject error;
            error["error"] = "Invalid JSON";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonObject obj = doc.object();
        QString code = obj["code"].toString();
        qDebug() << "[handleAuth] Code provided, length:" << code.length();
        
        if (code.isEmpty()) {
            qDebug() << "[handleAuth] Empty code";
            QJsonObject error;
            error["error"] = "Code is required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            m_blocker->recordAttempt(clientIP, false);
            return;
        }
        
        QString codeHash = QString::fromUtf8(
            QCryptographicHash::hash(code.toUtf8(), QCryptographicHash::Sha256).toHex()
        );
        
        bool isValid = (codeHash == m_accessCodeHash);
        qDebug() << "[handleAuth] Code valid:" << isValid;
        
        if (!isValid) {
            m_blocker->recordAttempt(clientIP, false);
            
            int remaining = m_blocker->getRemainingAttempts(clientIP);
            QJsonObject error;
            error["error"] = "Invalid code";
            error["remaining_attempts"] = remaining;
            error["is_blocked"] = m_blocker->isBlocked(clientIP);
            
            if (m_blocker->isBlocked(clientIP)) {
                error["blocked_until"] = m_blocker->getBlockedUntil(clientIP);
                error["message"] = "Too many failed attempts. IP blocked for 5 minutes.";
            }
            
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(401, "Unauthorized", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        m_blocker->recordAttempt(clientIP, true);
        
        std::string userId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        std::string token = m_jwtAuth->generateToken(userId);
        
        qDebug() << "[handleAuth] Generated token, length:" << token.length();
        
        QJsonObject response;
        response["success"] = true;
        response["token"] = QString::fromStdString(token);
        response["expires_in"] = 86400;
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
        
        qDebug() << "[handleAuth] Success for IP:" << clientIP;
    }
    
    void handleHealth(QTcpSocket* client) {
        qDebug() << "[handleHealth] Health check";
        QJsonObject response;
        response["status"] = "ok";
        response["service"] = "EMPI Agent";
        response["version"] = "1.0.0";
        response["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleStatus(QTcpSocket* client) {
        qDebug() << "[handleStatus] Status requested";
        QJsonObject response;
        response["threads_active"] = QThreadPool::globalInstance()->activeThreadCount();
        response["threads_max"] = QThreadPool::globalInstance()->maxThreadCount();
        
        QMutexLocker locker(&m_mutex);
        response["pending_tasks"] = static_cast<qint64>(m_pendingTasks.size());
        response["completed_tasks"] = static_cast<qint64>(m_results.size());
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleAdapt(QTcpSocket* client, const HttpRequest& req) {
        qDebug() << "[handleAdapt] Started";
        
        QString userId;
        if (!validateRequest(req, userId)) {
            qDebug() << "[handleAdapt] Unauthorized";
            QJsonObject error;
            error["error"] = "Unauthorized";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(401, "Unauthorized", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            qDebug() << "[handleAdapt] Invalid JSON";
            QJsonObject error;
            error["error"] = "Invalid JSON";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonObject obj = doc.object();
        QString text = obj["text"].toString();
        QString prompt = obj["prompt"].toString();
        
        qDebug() << "[handleAdapt] Text length:" << text.length() << "Prompt length:" << prompt.length();
        
        if (text.isEmpty()) {
            qDebug() << "[handleAdapt] Empty text";
            QJsonObject error;
            error["error"] = "Text is required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QString taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        
        {
            QMutexLocker locker(&m_mutex);
            m_pendingTasks.insert(taskId);
        }
        
        qDebug() << "[handleAdapt] Creating task:" << taskId;
        
        auto task = new AdaptationTask(
            text,
            prompt,
            m_llm,
            m_textAgent,
            m_feedbackAgent,
            m_interfaceGen,
            [this](const QString& taskId, const QString& result) {
                QMutexLocker locker(&m_mutex);
                m_results[taskId] = result;
                m_pendingTasks.remove(taskId);
                qDebug() << "[handleAdapt] Task completed:" << taskId;
            }
        );
        
        QThreadPool::globalInstance()->start(task);
        qDebug() << "[handleAdapt] Task started:" << taskId;
        
        QJsonObject response;
        response["task_id"] = taskId;
        response["status"] = "processing";
        response["result_url"] = QString("/api/result/%1").arg(taskId);
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(202, "Accepted", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleResult(QTcpSocket* client, const QString& taskId) {
        qDebug() << "[handleResult] Task ID:" << taskId;
        
        QMutexLocker locker(&m_mutex);
        
        auto it = m_results.find(taskId);
        if (it != m_results.end()) {
            QString html = it.value();
            m_results.erase(it);
            
            qDebug() << "[handleResult] Found completed task, HTML length:" << html.length();
            
            QJsonObject response;
            response["status"] = "completed";
            response["html"] = html;
            
            QByteArray body = QJsonDocument(response).toJson();
            client->write(buildHttpResponse(200, "OK", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        if (m_pendingTasks.contains(taskId)) {
            qDebug() << "[handleResult] Task still processing:" << taskId;
            QJsonObject response;
            response["status"] = "processing";
            
            QByteArray body = QJsonDocument(response).toJson();
            client->write(buildHttpResponse(200, "OK", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        qDebug() << "[handleResult] Task not found:" << taskId;
        QJsonObject response;
        response["status"] = "not_found";
        response["error"] = "Task not found or expired";
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(404, "Not Found", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleFetch(QTcpSocket* client, const HttpRequest& req) {
        qDebug() << "[handleFetch] Started";
        
        QString userId;
        if (!validateRequest(req, userId)) {
            qDebug() << "[handleFetch] Unauthorized";
            QJsonObject error;
            error["error"] = "Unauthorized";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(401, "Unauthorized", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            qDebug() << "[handleFetch] Invalid JSON";
            QJsonObject error;
            error["error"] = "Invalid JSON";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonObject obj = doc.object();
        QString url = obj["url"].toString();
        qDebug() << "[handleFetch] URL:" << url;
        
        if (url.isEmpty()) {
            qDebug() << "[handleFetch] Empty URL";
            QJsonObject error;
            error["error"] = "URL is required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        qDebug() << "[handleFetch] Fetching URL:" << url;
        QString content = fetchUrl(url);
        qDebug() << "[handleFetch] Fetch completed, content length:" << content.length();
        
        QJsonObject response;
        response["content"] = content;
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleParse(QTcpSocket* client, const HttpRequest& req) {
        qDebug() << "[handleParse] Started";
        
        QString userId;
        if (!validateRequest(req, userId)) {
            qDebug() << "[handleParse] Unauthorized";
            QJsonObject error;
            error["error"] = "Unauthorized";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(401, "Unauthorized", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            qDebug() << "[handleParse] Invalid JSON";
            QJsonObject error;
            error["error"] = "Invalid JSON";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonObject obj = doc.object();
        QString filename = obj["filename"].toString();
        QString base64Content = obj["content"].toString();
        
        qDebug() << "[handleParse] Filename:" << filename;
        qDebug() << "[handleParse] Base64 content length:" << base64Content.length();
        
        if (filename.isEmpty() || base64Content.isEmpty()) {
            qDebug() << "[handleParse] Empty filename or content";
            QJsonObject error;
            error["error"] = "Filename and content are required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        qDebug() << "[handleParse] Calling parseDocument...";
        QString result = parseDocument(filename, base64Content);
        qDebug() << "[handleParse] parseDocument returned, result length:" << result.length();
        qDebug() << "[handleParse] Result preview (first 200 chars):" << result.left(200);
        
        QJsonObject response;
        response["content"] = result;
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
        
        qDebug() << "[handleParse] Response sent";
    }
    
    void handleStaticFile(QTcpSocket* client, const QString& path) {
        QString filePath = "gui/web" + path;
        if (path == "/") filePath = "gui/web/index.html";
        
        qDebug() << "[handleStaticFile] Serving:" << filePath;
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "[handleStaticFile] File not found:" << filePath;
            QJsonObject error;
            error["error"] = "File not found: " + path;
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(404, "Not Found", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QByteArray content = file.readAll();
        
        QString mimeType = "text/html";
        if (filePath.endsWith(".css")) mimeType = "text/css";
        else if (filePath.endsWith(".js")) mimeType = "application/javascript";
        else if (filePath.endsWith(".json")) mimeType = "application/json";
        else if (filePath.endsWith(".png")) mimeType = "image/png";
        else if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) mimeType = "image/jpeg";
        else if (filePath.endsWith(".svg")) mimeType = "image/svg+xml";
        else if (filePath.endsWith(".ico")) mimeType = "image/x-icon";
        
        client->write(buildHttpResponse(200, "OK", mimeType, content));
        client->flush();
        client->disconnectFromHost();
        
        qDebug() << "[handleStaticFile] Served:" << filePath << "size:" << content.size();
    }
    
    QString fetchUrl(const QString& url) {
        qDebug() << "[fetchUrl] Fetching:" << url;
        
        QUrl qurl(url);
        if (!qurl.isValid() || (qurl.scheme() != "http" && qurl.scheme() != "https")) {
            qDebug() << "[fetchUrl] Invalid URL scheme";
            return "Error: Invalid or unsupported URL scheme";
        }
        
        QNetworkRequest request(qurl);
        request.setHeader(QNetworkRequest::UserAgentHeader, 
                         "EMPI-Agent/1.0 (+http://empi.agent)");
        request.setRawHeader("Accept", "text/html,application/xhtml+xml");
        
        QNetworkReply* reply = m_networkManager->get(request);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(30000);
        
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        qDebug() << "[fetchUrl] Waiting for response...";
        loop.exec();
        
        if (!timer.isActive()) {
            qDebug() << "[fetchUrl] Timeout (30s)";
            reply->abort();
            reply->deleteLater();
            return "Error: Request timeout (30 seconds)";
        }
        
        timer.stop();
        
        if (reply->error() != QNetworkReply::NoError) {
            QString error = QString("Error: %1").arg(reply->errorString());
            qDebug() << "[fetchUrl] Network error:" << error;
            reply->deleteLater();
            return error;
        }
        
        QByteArray data = reply->readAll();
        reply->deleteLater();
        
        qDebug() << "[fetchUrl] Received" << data.size() << "bytes";
        
        if (data.startsWith("<!DOCTYPE") || data.startsWith("<html")) {
            qDebug() << "[fetchUrl] Detected HTML content";
            QString html = QString::fromUtf8(data);
            QRegularExpression scriptRegex("<script[^>]*>[\\s\\S]*?</script>", 
                                          QRegularExpression::CaseInsensitiveOption);
            html.remove(scriptRegex);
            return html;
        }
        
        qDebug() << "[fetchUrl] Returning raw content";
        return QString::fromUtf8(data);
    }
    
    QString parseDocument(const QString& filename, const QString& base64Content) {
        qDebug() << "[parseDocument] Started for:" << filename;
        
        try {
            QByteArray content = QByteArray::fromBase64(base64Content.toUtf8());
            qDebug() << "[parseDocument] Decoded size:" << content.size() << "bytes";
            
            if (content.size() > 50 * 1024 * 1024) {
                qDebug() << "[parseDocument] File too large:" << content.size();
                return "Error: File too large (max 50MB)";
            }
            
            QString uniqueFilename = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") 
                                     + "_" + filename;
            QString tempPath = QDir::temp().absoluteFilePath(uniqueFilename);
            
            qDebug() << "[parseDocument] Temp file:" << tempPath;
            
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                qDebug() << "[parseDocument] Cannot create temp file";
                return "Error: Cannot create temporary file";
            }
            
            tempFile.write(content);
            tempFile.close();
            
            qDebug() << "[parseDocument] Calling DocumentParser::parseFile";
            QString result = m_docParser->parseFile(tempPath);
            qDebug() << "[parseDocument] parseFile returned, result length:" << result.length();
            
            tempFile.remove();
            qDebug() << "[parseDocument] Temp file removed";
            
            if (result.isEmpty()) {
                qDebug() << "[parseDocument] Warning: Empty result";
                return "Warning: Document parsed but no text content extracted";
            }
            
            return result;
            
        } catch (const std::exception& e) {
            qDebug() << "[parseDocument] Exception:" << e.what();
            return QString("Error: %1").arg(e.what());
        }
    }
    
    std::shared_ptr<EMPI::LLMClient> m_llm;
    std::shared_ptr<EMPI::TextAnalyzer> m_textAgent;
    std::shared_ptr<EMPI::FeedbackAgent> m_feedbackAgent;
    std::shared_ptr<EMPI::InterfaceGenerator> m_interfaceGen;
    std::shared_ptr<DocumentParser> m_docParser;
    QNetworkAccessManager* m_networkManager;
    QTcpServer* m_server = nullptr;
    
    std::unique_ptr<JwtAuth> m_jwtAuth;
    std::unique_ptr<AuthBlocker> m_blocker;
    QString m_accessCodeHash;
    
    QMutex m_mutex;
    QSet<QString> m_pendingTasks;
    QMap<QString, QString> m_results;
};

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    std::string configPath = "config/agent_config.toml";
    int port = 8080;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configPath = argv[++i];
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        } else if (arg == "-h" || arg == "--help") {
            qDebug() << "EMPI Agent HTTP Server (Qt5 + JWT + Block)";
            qDebug() << "Usage: ./empi_http [options]";
            qDebug() << "  -c, --config <path>  Config file path (default: config/agent_config.toml)";
            qDebug() << "  -p, --port <port>    HTTP port (default: 8080)";
            qDebug() << "  -h, --help           Show this help";
            return 0;
        }
    }
    
    qDebug() << "[main] Starting EMPI Agent HTTP Server (Qt5 + JWT + Block)...";
    qDebug() << "[main] Config:" << configPath.c_str();
    qDebug() << "[main] Port:" << port;
    
    HttpServer server(configPath);
    server.start(port);
    
    qDebug() << "[main] Entering event loop...";
    return app.exec();
}

#include "main.moc"
