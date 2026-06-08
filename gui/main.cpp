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
#include <QTemporaryDir>
#include <QProcess>
#include <QDebug>
#include <future>
#include <filesystem>
#include <fstream>
#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <poppler-qt5.h>

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

class DocumentParser : public QObject {
    Q_OBJECT
public:
    explicit DocumentParser(QObject *parent = nullptr) : QObject(parent), libreOfficeProcess(nullptr) {}
    
    QString parseFile(const QString& filePath) {
        QFileInfo info(filePath);
        QString ext = info.suffix().toLower();
        
        if (ext == "pdf") {
            return parsePdf(filePath);
        } 
        else if (ext == "docx" || ext == "doc") {
            return parseDocx(filePath);
        }
        else if (ext == "txt" || ext == "html" || ext == "htm" || ext == "md") {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                QTextStream stream(&file);
                return stream.readAll();
            }
            return "Error: Cannot open text file";
        }
        
        return QString("Error: Unsupported file format: %1 (supported: PDF, DOCX, DOC, TXT, HTML, MD)").arg(ext);
    }
    
private:
    QString parsePdf(const QString& filePath) {
        Poppler::Document* document = Poppler::Document::load(filePath);
        
        if (!document || document->isLocked()) {
            delete document;
            return "Error: Cannot open PDF file (possibly password protected or corrupted)";
        }
        
        QString allText;
        int numPages = document->numPages();
        
        for (int i = 0; i < numPages; ++i) {
            Poppler::Page* page = document->page(i);
            if (page) {
                QString pageText = page->text(Poppler::Page::PhysicalLayout);
                if (!pageText.isEmpty()) {
                    allText += pageText + "\n\n";
                }
                delete page;
            }
        }
        
        delete document;
        
        if (allText.isEmpty()) {
            return "Error: No text content extracted from PDF (may be image-based or scanned)";
        }
        
        return allText;
    }
    
    bool hasLibreOffice() const {
        QProcess process;
        process.start("soffice", QStringList() << "--version");
        process.waitForFinished(3000);
        return process.exitCode() == 0;
    }
    
    QString parseDocx(const QString& filePath) {
        if (!hasLibreOffice()) {
            return "Error: LibreOffice not installed. Please install LibreOffice to parse DOCX/DOC files.\n"
                   "Linux: sudo apt install libreoffice\n"
                   "macOS: brew install libreoffice\n"
                   "Windows: Download from libreoffice.org";
        }
        
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            return "Error: Cannot create temporary directory";
        }
        
        QProcess process;
        process.start("soffice", QStringList() 
                      << "--headless"
                      << "--convert-to" << "html"
                      << "--outdir" << tempDir.path()
                      << filePath);
        
        if (!process.waitForFinished(30000)) {
            process.kill();
            return "Error: LibreOffice conversion timeout (30 seconds)";
        }
        
        if (process.exitCode() != 0) {
            QString error = QString::fromUtf8(process.readAllStandardError());
            return QString("Error: LibreOffice conversion failed: %1").arg(error);
        }
        
        QFileInfo inputInfo(filePath);
        QString baseName = inputInfo.completeBaseName();
        QString htmlPath = tempDir.path() + "/" + baseName + ".html";
        
        return extractTextFromHtml(htmlPath);
    }
    
    QString extractTextFromHtml(const QString& htmlPath) {
        QFile file(htmlPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return "Error: Cannot read converted HTML file";
        }
        
        QString html = QString::fromUtf8(file.readAll());
        file.close();
        
        // Simple HTML stripping
        QString text;
        bool inTag = false;
        bool inScript = false;
        
        for (int i = 0; i < html.length(); ++i) {
            QChar ch = html[i];
            
            if (ch == '<') {
                inTag = true;
                if (html.mid(i, 7).toLower() == "<script" ||
                    html.mid(i, 6).toLower() == "<style") {
                    inScript = true;
                }
                continue;
            }
            
            if (ch == '>') {
                inTag = false;
                if (inScript && (html.mid(i-8, 9).toLower() == "</script>" ||
                                html.mid(i-7, 8).toLower() == "</style>")) {
                    inScript = false;
                }
                if (i > 0 && html[i-1] == '>') {
                    QString tag = html.mid(html.lastIndexOf('<', i-1), i - html.lastIndexOf('<', i-1));
                    if (tag.contains("p") || tag.contains("div") || tag.contains("br") || tag.contains("h")) {
                        text += "\n\n";
                    }
                }
                continue;
            }
            
            if (!inTag && !inScript && !ch.isSpace()) {
                text += ch;
            } else if (!inTag && !inScript && ch == ' ' && !text.endsWith(' ')) {
                text += ' ';
            }
        }
        
        text = text.simplified();
        
        if (text.isEmpty()) {
            return "Warning: No text content extracted from DOCX file";
        }
        
        return text;
    }
    
    QProcess* libreOfficeProcess;
};

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

    Q_INVOKABLE void adapt(const QString& text, const QString& prompt) {
        QtConcurrent::run([this, text, prompt]() {
            // Check if text looks like binary
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
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        reply->deleteLater();
        
        // Check if it's HTML
        if (contentType.contains("text/html") || data.startsWith("<!DOCTYPE") || data.startsWith("<html")) {
            QString html = QString::fromUtf8(data);
            return sanitizeHtmlForSafety(html);
        }
        
        // Return as text for other content types
        return QString::fromUtf8(data);
    }
    
    Q_INVOKABLE QString parseDocument(const QString& filePath) {
        return docParser->parseFile(filePath);
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
    
private:
    QString stripMarkdownBlocks(const QString& html) {
        QString result = html;
        result.remove(QRegularExpression("^```(?:html)?\\s*\\n?", 
                                         QRegularExpression::CaseInsensitiveOption));
        result.remove(QRegularExpression("\\n?```\\s*$"));
        return result.trimmed();
    }
    
    QString sanitizeHtmlForSafety(const QString& html) {
        if (html.isEmpty()) return html;
        
        // Remove script tags and their contents
        QString result = html;
        QRegularExpression scriptRegex("<script[^>]*>[\\s\\S]*?</script>", 
                                        QRegularExpression::CaseInsensitiveOption);
        result.remove(scriptRegex);
        
        // Remove iframe, object, embed tags
        QRegularExpression iframeRegex("<iframe[^>]*>[\\s\\S]*?</iframe>", 
                                       QRegularExpression::CaseInsensitiveOption);
        result.remove(iframeRegex);
        
        QRegularExpression objectRegex("<object[^>]*>[\\s\\S]*?</object>", 
                                       QRegularExpression::CaseInsensitiveOption);
        result.remove(objectRegex);
        
        QRegularExpression embedRegex("<embed[^>]*>", 
                                      QRegularExpression::CaseInsensitiveOption);
        result.remove(embedRegex);
        
        // Remove javascript: links
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
    
    // Configure web security
    QWebEngineProfile* profile = page->profile();
    profile->setHttpCacheType(QWebEngineProfile::NoCache);
    
    // Intercept navigation requests for security
    QObject::connect(page, &QWebEnginePage::navigationRequested,
        [&](QWebEngineNavigationRequest &request) {
            QUrl url = request.url();
            QString scheme = url.scheme();
            
            // Block file:// access completely
            if (scheme == "file") {
                request.reject();
                qWarning() << "[Security] Blocked file:// navigation to:" << url.toString();
                return;
            }
            
            // Allow http/https internally
            if (scheme == "http" || scheme == "https") {
                // Check if external link (different domain)
                QString currentHost = view.url().host();
                if (url.host() != currentHost && 
                    request.navigationType() == QWebEngineNavigationRequest::NavigationTypeLinkClicked) {
                    request.reject();
                    QDesktopServices::openUrl(url);
                    qDebug() << "[Security] Opened external link in system browser:" << url.toString();
                }
                // else: allow internal navigation
            }
            else if (scheme == "about" || scheme == "data" || scheme == "blob") {
                // Allow these internal schemes
            }
            else {
                // Reject unknown schemes
                request.reject();
                qWarning() << "[Security] Blocked unknown scheme:" << scheme << "for URL:" << url.toString();
            }
        });
    
    view.setWindowTitle("EMPI Agent");
    view.resize(1200, 800);

    // Load HTML interface
    QString htmlPath = "gui/web/index.html";
    QFile file(htmlPath);
    if (file.open(QIODevice::ReadOnly)) {
        QString basePath = QFileInfo(htmlPath).absolutePath() + "/";
        view.setHtml(file.readAll(), QUrl::fromLocalFile(basePath));
    } else {
        qWarning() << "Could not open HTML file:" << htmlPath;
        // Fallback to simple HTML
        view.setHtml("<html><body><h1>EMPI Agent</h1><p>Interface file not found. Please check installation.</p></body></html>");
    }

    view.show();
    return app.exec();
}

#include "main.moc"
