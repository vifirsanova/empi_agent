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
};

Config load_config(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        qWarning() << "Config file not found:" << path.c_str();
        return cfg;
    }
    
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        if (section != "llm") continue;
        
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
    }
    return cfg;
}

// ============================================================================
// HTTP PARSER HELPERS
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
    
    // Parse request line
    QStringList parts = lines[0].split(" ");
    if (parts.size() >= 3) {
        req.method = parts[0];
        req.path = parts[1];
        
        // Parse query params
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
    
    // Parse headers and find body
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
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
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
            }
            
            json dialog = json::array();
            if (!m_prompt.isEmpty()) {
                dialog.push_back({{"role", "user"}, {"content", m_prompt.toStdString()}});
                dialog.push_back({{"role", "assistant"}, {"content", "Adapting..."}});
            } else {
                dialog.push_back({{"role", "user"}, {"content", "Please adapt this text."}});
            }
            
            auto f1 = std::async(std::launch::async, [&]() {
                return m_textAgent->process_raw({{"text", cleanText.toStdString()}});
            });
            auto f2 = std::async(std::launch::async, [&]() {
                return m_feedbackAgent->process_raw({{"dialog_history", dialog}});
            });
            
            json m = f1.get()["payload"]["data"];
            json f = f2.get()["payload"]["data"];
            
            json ig = {
                {"text_metrics", m.value("metrics", json::object())},
                {"feedback_analysis", f.value("analysis", json::object())},
                {"original_text", cleanText.toStdString()}
            };
            
            json r = m_interfaceGen->process_raw(ig);
            QString html = QString::fromStdString(r["payload"]["data"].value("html", ""));
            
            html = stripMarkdownBlocks(html);
            html = sanitizeHtml(html);
            
            m_callback(taskId, html);
            
        } catch (const std::exception& e) {
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
// HTTP SERVER (Qt5 version)
// ============================================================================

class HttpServer : public QObject {
    Q_OBJECT
public:
    HttpServer(const std::string& configPath, QObject* parent = nullptr) 
        : QObject(parent) {
        
        Config cfg = load_config(configPath);
        
        m_llm = std::make_shared<EMPI::LLMClient>("python3");
        m_textAgent = std::make_shared<EMPI::TextAnalyzer>();
        m_feedbackAgent = std::make_shared<EMPI::FeedbackAgent>(m_llm);
        m_interfaceGen = std::make_shared<EMPI::InterfaceGenerator>(m_llm, cfg.local_model_path);
        m_docParser = std::make_shared<DocumentParser>();
        m_networkManager = new QNetworkAccessManager(this);
        
        QThreadPool::globalInstance()->setMaxThreadCount(
            std::max(1, QThread::idealThreadCount())
        );
        
        qDebug() << "HTTP Server initialized with" 
                 << QThreadPool::globalInstance()->maxThreadCount() 
                 << "threads";
    }

    void start(int port = 8080) {
        m_server = new QTcpServer(this);
        
        connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
        
        if (!m_server->listen(QHostAddress::Any, port)) {
            qCritical() << "Failed to listen on port" << port;
            return;
        }
        
        qDebug() << "========================================";
        qDebug() << "EMPI Agent HTTP Server started (Qt5)";
        qDebug() << "========================================";
        qDebug() << "Port:" << port;
        qDebug() << "Threads:" << QThreadPool::globalInstance()->maxThreadCount();
        qDebug() << "----------------------------------------";
        qDebug() << "Endpoints:";
        qDebug() << "  GET  /api/health         - Health check";
        qDebug() << "  GET  /api/status         - Server status";
        qDebug() << "  POST /api/adapt          - Adapt text (async)";
        qDebug() << "  GET  /api/result/<id>    - Get result";
        qDebug() << "  POST /api/fetch          - Fetch URL";
        qDebug() << "  POST /api/parse          - Parse document";
        qDebug() << "========================================";
    }

private slots:
    void onNewConnection() {
        QTcpSocket* client = m_server->nextPendingConnection();
        if (!client) return;
        
        connect(client, &QTcpSocket::readyRead, this, [this, client]() {
            handleClient(client);
        });
        
        connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
    }

private:
    void handleClient(QTcpSocket* client) {
        QByteArray data = client->readAll();
        if (data.isEmpty()) return;
        
        HttpRequest req = parseHttpRequest(data);
        
        qDebug() << "Request:" << req.method << req.path;
        
        // Handle OPTIONS (CORS preflight)
        if (req.method == "OPTIONS") {
            client->write(buildHttpResponse(204, "No Content", "text/plain", QByteArray()));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        // Routes
        if (req.path == "/api/health") {
            handleHealth(client);
        } else if (req.path == "/api/status") {
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
            // Static files
            handleStaticFile(client, req.path);
        }
    }
    
    void handleHealth(QTcpSocket* client) {
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
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
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
        
        if (text.isEmpty()) {
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
                qDebug() << "Task completed:" << taskId;
            }
        );
        
        QThreadPool::globalInstance()->start(task);
        qDebug() << "Task started:" << taskId;
        
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
        QMutexLocker locker(&m_mutex);
        
        auto it = m_results.find(taskId);
        if (it != m_results.end()) {
            QString html = it.value();
            m_results.erase(it);
            
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
            QJsonObject response;
            response["status"] = "processing";
            
            QByteArray body = QJsonDocument(response).toJson();
            client->write(buildHttpResponse(200, "OK", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QJsonObject response;
        response["status"] = "not_found";
        response["error"] = "Task not found or expired";
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(404, "Not Found", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleFetch(QTcpSocket* client, const HttpRequest& req) {
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
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
        
        if (url.isEmpty()) {
            QJsonObject error;
            error["error"] = "URL is required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QString content = fetchUrl(url);
        
        QJsonObject response;
        response["content"] = content;
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleParse(QTcpSocket* client, const HttpRequest& req) {
        QJsonDocument doc = QJsonDocument::fromJson(req.body.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
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
        
        if (filename.isEmpty() || base64Content.isEmpty()) {
            QJsonObject error;
            error["error"] = "Filename and content are required";
            QByteArray body = QJsonDocument(error).toJson();
            client->write(buildHttpResponse(400, "Bad Request", "application/json", body));
            client->flush();
            client->disconnectFromHost();
            return;
        }
        
        QString result = parseDocument(filename, base64Content);
        
        QJsonObject response;
        response["content"] = result;
        
        QByteArray body = QJsonDocument(response).toJson();
        client->write(buildHttpResponse(200, "OK", "application/json", body));
        client->flush();
        client->disconnectFromHost();
    }
    
    void handleStaticFile(QTcpSocket* client, const QString& path) {
        QString filePath = "gui/web" + path;
        if (path == "/") filePath = "gui/web/index.html";
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
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
    }
    
    // ========================================================================
    // HELPERS
    // ========================================================================
    
    QString fetchUrl(const QString& url) {
        QUrl qurl(url);
        if (!qurl.isValid() || (qurl.scheme() != "http" && qurl.scheme() != "https")) {
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
        
        loop.exec();
        
        if (!timer.isActive()) {
            reply->abort();
            reply->deleteLater();
            return "Error: Request timeout (30 seconds)";
        }
        
        timer.stop();
        
        if (reply->error() != QNetworkReply::NoError) {
            QString error = QString("Error: %1").arg(reply->errorString());
            reply->deleteLater();
            return error;
        }
        
        QByteArray data = reply->readAll();
        reply->deleteLater();
        
        if (data.startsWith("<!DOCTYPE") || data.startsWith("<html")) {
            QString html = QString::fromUtf8(data);
            QRegularExpression scriptRegex("<script[^>]*>[\\s\\S]*?</script>", 
                                          QRegularExpression::CaseInsensitiveOption);
            html.remove(scriptRegex);
            return html;
        }
        
        return QString::fromUtf8(data);
    }
    
    QString parseDocument(const QString& filename, const QString& base64Content) {
        try {
            QByteArray content = QByteArray::fromBase64(base64Content.toUtf8());
            
            if (content.size() > 50 * 1024 * 1024) {
                return "Error: File too large (max 50MB)";
            }
            
            QString uniqueFilename = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") 
                                     + "_" + filename;
            QString tempPath = QDir::temp().absoluteFilePath(uniqueFilename);
            
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                return "Error: Cannot create temporary file";
            }
            
            tempFile.write(content);
            tempFile.close();
            
            QString result = m_docParser->parseFile(tempPath);
            tempFile.remove();
            
            return result;
            
        } catch (const std::exception& e) {
            return QString("Error: %1").arg(e.what());
        }
    }
    
    // ========================================================================
    // MEMBERS
    // ========================================================================
    
    std::shared_ptr<EMPI::LLMClient> m_llm;
    std::shared_ptr<EMPI::TextAnalyzer> m_textAgent;
    std::shared_ptr<EMPI::FeedbackAgent> m_feedbackAgent;
    std::shared_ptr<EMPI::InterfaceGenerator> m_interfaceGen;
    std::shared_ptr<DocumentParser> m_docParser;
    QNetworkAccessManager* m_networkManager;
    QTcpServer* m_server = nullptr;
    
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
            qDebug() << "EMPI Agent HTTP Server (Qt5)";
            qDebug() << "Usage: ./empi_http [options]";
            qDebug() << "  -c, --config <path>  Config file path (default: config/agent_config.toml)";
            qDebug() << "  -p, --port <port>    HTTP port (default: 8080)";
            qDebug() << "  -h, --help           Show this help";
            return 0;
        }
    }
    
    qDebug() << "Starting EMPI Agent HTTP Server (Qt5)...";
    qDebug() << "Config:" << configPath.c_str();
    qDebug() << "Port:" << port;
    
    HttpServer server(configPath);
    server.start(port);
    
    return app.exec();
}

#include "main.moc"
