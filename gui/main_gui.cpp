#include <QApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineNavigationRequest>
#include <QWebChannel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QDir>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <future>
#include <filesystem>
#include <fstream>
#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"
#include <QFutureWatcher>
#include <QtConcurrent>
#include "DocumentParser.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

struct Config {
    std::string api_key, api_base, folder_id, cloud_model, local_model_path;
};

Config load_config(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        if (section != "llm") continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
        if (!val.empty() && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        if (key == "api_key") cfg.api_key = val;
        else if (key == "api_base") cfg.api_base = val;
        else if (key == "folder_id") cfg.folder_id = val;
        else if (key == "model") cfg.cloud_model = val;
        else if (key == "local_model_path") cfg.local_model_path = val;
    }
    return cfg;
}

class EmpiBridge : public QObject {
    Q_OBJECT
    std::shared_ptr<EMPI::LLMClient> llm;
    std::unique_ptr<EMPI::TextAnalyzer> textAgent;
    std::unique_ptr<EMPI::FeedbackAgent> feedbackAgent;
    std::unique_ptr<EMPI::InterfaceGenerator> interfaceGen;
    QNetworkAccessManager* networkManager;
    DocumentParser* docParser;

public:
    EmpiBridge(const std::string& configPath, QObject* parent = nullptr) : QObject(parent) {
        Config cfg = load_config(configPath);
        llm = std::make_shared<EMPI::LLMClient>("python3");
        textAgent = std::make_unique<EMPI::TextAnalyzer>();
        feedbackAgent = std::make_unique<EMPI::FeedbackAgent>(llm);
        interfaceGen = std::make_unique<EMPI::InterfaceGenerator>(llm, cfg.local_model_path);
        networkManager = new QNetworkAccessManager(this);
        docParser = new DocumentParser(this);
    }

    Q_INVOKABLE void parseDocumentFromContent(const QString& filename, const QString& base64Content) {
    // Decode base64 to binary
    QByteArray content = QByteArray::fromBase64(base64Content.toUtf8());
    
    // Add unique ID to filename to prevent conflicts
    QString uniqueFilename = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") + "_" + filename;
    QString tempPath = QDir::temp().absoluteFilePath(uniqueFilename);
    
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly)) {
        tempFile.write(content);
        tempFile.close();
        
        QString result = docParser->parseFile(tempPath);
        tempFile.remove();
        
        emit documentParsed(result);
    } else {
        emit documentParsed("Error: Cannot create temporary file");
    }
}
    Q_INVOKABLE void adapt(const QString& text, const QString& prompt) {
        auto future = QtConcurrent::run([this, text, prompt]() {
            bool isBinary = false;
            for (QChar ch : text) {
                if (ch.unicode() == 0 || (ch.unicode() < 32 && ch.unicode() != 10 && ch.unicode() != 13 && ch.unicode() != 9)) {
                    isBinary = true;
                    break;
                }
            }
            
            QString cleanText = text;
            if (isBinary) {
                cleanText = "[Warning: Binary content detected. Please upload PDF, DOCX, or text files for proper parsing.]\n\n" + text.left(500);
            }
            
            json dialog = json::array();
            if (!prompt.isEmpty()) {
                dialog.push_back({{"role", "user"}, {"content", prompt.toStdString()}});
                dialog.push_back({{"role", "assistant"}, {"content", "Adapting..."}});
            } else {
                dialog.push_back({{"role", "user"}, {"content", "Please adapt this text."}});
            }
            
            auto f1 = std::async(std::launch::async, [&]() {
                return textAgent->process_raw({{"text", cleanText.toStdString()}});
            });
            auto f2 = std::async(std::launch::async, [&]() {
                return feedbackAgent->process_raw({{"dialog_history", dialog}});
            });
            
            json m = f1.get()["payload"]["data"];
            json f = f2.get()["payload"]["data"];
            
            json ig = {
                {"text_metrics", m.value("metrics", json::object())},
                {"feedback_analysis", f.value("analysis", json::object())},
                {"original_text", cleanText.toStdString()}
            };
            
            json r = interfaceGen->process_raw(ig);
            QString html = QString::fromStdString(r["payload"]["data"].value("html", ""));
            
            html = stripMarkdownBlocks(html);
            html = sanitizeHtmlForSafety(html);
            
            emit adaptationComplete(html);
        });
        (void)future;
    }
    
    Q_INVOKABLE QString fetchUrl(const QString& url) {
        QUrl qurl(url);
        if (!qurl.isValid() || (qurl.scheme() != "http" && qurl.scheme() != "https")) {
            return QString("Error: Invalid or unsupported URL scheme (only HTTP/HTTPS allowed)");
        }
        
        QNetworkRequest request(qurl);
        request.setHeader(QNetworkRequest::UserAgentHeader, 
                         "Mozilla/5.0 (EMPI Agent/1.0; +http://empi.agent)");
        
        QNetworkReply* reply = networkManager->get(request);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        timer.start(30000);
        loop.exec();
        
        if (!timer.isActive()) {
            reply->abort();
            reply->deleteLater();
            return QString("Error: Request timeout (30 seconds)");
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
            return sanitizeHtmlForSafety(html);
        }
        
        return QString::fromUtf8(data);
    }
    
    Q_INVOKABLE bool openExternalUrl(const QString& url) {
        QUrl qurl(url);
        if (qurl.scheme() == "http" || qurl.scheme() == "https") {
            return QDesktopServices::openUrl(qurl);
        }
        return false;
    }
    
signals:
    void adaptationComplete(const QString& html);
    void documentParsed(const QString& result);
    
private:
    QString stripMarkdownBlocks(const QString& html) {
        QString result = html;
        QRegularExpression re("^```(?:html)?\\s*\\n?", QRegularExpression::CaseInsensitiveOption);
        result.remove(re);
        result.remove(QRegularExpression("\\n?```\\s*$"));
        return result.trimmed();
    }
    
    QString sanitizeHtmlForSafety(const QString& html) {
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
        
        return result;
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    std::string configPath = "config/agent_config.toml";
    if (argc > 1) configPath = argv[1];

    EmpiBridge bridge(configPath);

    QWebChannel channel;
    channel.registerObject("backend", &bridge);

    QWebEngineView view;
    QWebEnginePage* page = view.page();
    page->setWebChannel(&channel);
    
    QObject::connect(page, &QWebEnginePage::navigationRequested,
        [&](QWebEngineNavigationRequest &request) {
            QUrl url = request.url();
            QString scheme = url.scheme();
            
            if (scheme == "file") {
                request.reject();
                qWarning() << "[Security] Blocked file:// navigation to:" << url.toString();
                return;
            }
            
            if (scheme == "http" || scheme == "https") {
                QString currentHost = view.url().host();
                if (url.host() != currentHost && request.navigationType() == 0) {
                    request.reject();
                    QDesktopServices::openUrl(url);
                    qDebug() << "[Security] Opened external link in system browser:" << url.toString();
                }
            }
            else if (scheme == "about" || scheme == "data" || scheme == "blob") {
                // Allow these
            }
            else {
                request.reject();
                qWarning() << "[Security] Blocked unknown scheme:" << scheme;
            }
        });
    
    view.setWindowTitle("EMPI Agent");
    view.resize(1200, 800);

    QString htmlPath = "gui/web/index.html";
    QFile file(htmlPath);
    if (file.open(QIODevice::ReadOnly)) {
        QString basePath = QFileInfo(htmlPath).absolutePath() + "/";
        view.setHtml(file.readAll(), QUrl::fromLocalFile(basePath));
    } else {
        qWarning() << "Could not open HTML file:" << htmlPath;
        view.setHtml("<html><body><h1>EMPI Agent</h1><p>Interface file not found. Please check installation.</p></body></html>");
    }

    view.show();
    return app.exec();
}

#include "main.moc"

