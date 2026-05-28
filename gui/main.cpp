#include <QApplication>
#include <QWebEngineView>
#include <QWebChannel>
#include <QFile>
#include <QFileInfo>
#include <future>
#include <filesystem>
#include <fstream>
#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"

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

public:
    EmpiBridge(const std::string& configPath) {
        Config cfg = load_config(configPath);
        llm = std::make_shared<EMPI::LLMClient>("python3");
        textAgent = std::make_unique<EMPI::TextAnalyzer>();
        feedbackAgent = std::make_unique<EMPI::FeedbackAgent>(llm);
        interfaceGen = std::make_unique<EMPI::InterfaceGenerator>(llm, cfg.local_model_path);
    }

    Q_INVOKABLE QString adapt(const QString& text, const QString& prompt) {
        json dialog = json::array();
        if (!prompt.isEmpty()) {
            dialog.push_back({{"role", "user"}, {"content", prompt.toStdString()}});
            dialog.push_back({{"role", "assistant"}, {"content", "Adapting..."}});
        } else {
            dialog.push_back({{"role", "user"}, {"content", "Please adapt this text."}});
        }

        auto f1 = std::async(std::launch::async, [&]() {
            return textAgent->process_raw({{"text", text.toStdString()}});
        });
        auto f2 = std::async(std::launch::async, [&]() {
            return feedbackAgent->process_raw({{"dialog_history", dialog}});
        });

        json m = f1.get()["payload"]["data"];
        json f = f2.get()["payload"]["data"];

        json ig = {
            {"text_metrics", m.value("metrics", json::object())},
            {"feedback_analysis", f.value("analysis", json::object())},
            {"original_text", text.toStdString()}
        };

        json r = interfaceGen->process_raw(ig);
        return QString::fromStdString(r["payload"]["data"].value("html", ""));
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
    view.page()->setWebChannel(&channel);
    view.setWindowTitle("EMPI Agent");
    view.resize(1000, 700);

    QString htmlPath = "gui/web/index.html";
    QFile file(htmlPath);
    if (file.open(QIODevice::ReadOnly)) {
        QString basePath = QFileInfo(htmlPath).absolutePath() + "/";
        view.setHtml(file.readAll(), QUrl::fromLocalFile(basePath));
    }

    view.show();
    return app.exec();
}

#include "main.moc"
