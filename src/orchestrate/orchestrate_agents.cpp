#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <filesystem>
#include <cstring>

using namespace EMPI;
using json = nlohmann::json;
namespace fs = std::filesystem;

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
    if (!f.is_open()) return cfg;

    std::string line;
    std::string current_section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            current_section = line.substr(1, line.find(']') - 1);
            continue;
        }
        if (current_section != "llm") continue;

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

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path = "output/index.html";
    std::string prompt;
    std::string config_path = "config/agent_config.toml";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "EMPI Agent Orchestration\n\n"
                      << "Usage: orchestrate_agents -i <input.txt> [options]\n\n"
                      << "Options:\n"
                      << "  -i, --input <path>    Input text file (required)\n"
                      << "  -o, --output <path>   Output HTML path (default: output/index.html)\n"
                      << "  -p, --prompt <text>   User prompt for adaptation\n"
                      << "  -c, --config <path>   Config file (default: config/agent_config.toml)\n"
                      << "  -h, --help            Show this help\n";
            return 0;
        }
        else if (arg == "-i" || arg == "--input") { if (i + 1 < argc) input_path = argv[++i]; }
        else if (arg == "-o" || arg == "--output") { if (i + 1 < argc) output_path = argv[++i]; }
        else if (arg == "-p" || arg == "--prompt") { if (i + 1 < argc) prompt = argv[++i]; }
        else if (arg == "-c" || arg == "--config") { if (i + 1 < argc) config_path = argv[++i]; }
    }

    if (input_path.empty()) {
        std::cerr << "Error: input file required. Use -h for help.\n";
        return 1;
    }

    Config cfg = load_config(config_path);
    auto llm = std::make_shared<LLMClient>("python3");

    bool cloud_ok = !cfg.api_key.empty();
    bool local_ok = !cfg.local_model_path.empty() && fs::exists(cfg.local_model_path);

    std::cout << "Cloud API: " << (cloud_ok ? "configured" : "not set") << std::endl;
    std::cout << "Local model: " << (local_ok ? cfg.local_model_path : "not found") << std::endl;

    TextAnalyzer text_agent;
    FeedbackAgent feedback_agent(llm);
    InterfaceGenerator interface_gen(llm, cfg.local_model_path);

    std::string text = read_file(input_path);

    json dialog = json::array();
    if (!prompt.empty()) {
        dialog.push_back({{"role", "user"}, {"content", prompt}});
        dialog.push_back({{"role", "assistant"}, {"content", "I will adapt the material based on your needs."}});
    } else {
        dialog.push_back({{"role", "user"}, {"content", "Please adapt this text for accessibility."}});
        dialog.push_back({{"role", "assistant"}, {"content", "Analyzing and adapting the content."}});
    }

    std::cout << "Running TextAnalyzer + FeedbackAgent in parallel..." << std::endl;

    auto future_text = std::async(std::launch::async, [&]() {
        return text_agent.process_raw({{"text", text}});
    });
    auto future_feedback = std::async(std::launch::async, [&]() {
        return feedback_agent.process_raw({{"dialog_history", dialog}});
    });

    json text_result = future_text.get();
    json feedback_result = future_feedback.get();
    json metrics = text_result["payload"]["data"];
    json feedback = feedback_result["payload"]["data"];

    if (metrics["status"] == "success") {
        std::cout << "TextAnalyzer: " << metrics.value("complexity_label", "?") << std::endl;
    } else {
        std::cerr << "TextAnalyzer failed: " << metrics.value("message", "unknown") << std::endl;
    }
    if (feedback["status"] == "success") {
        std::cout << "FeedbackAgent: sentiment=" << feedback["analysis"].value("sentiment", "?") << std::endl;
    } else {
        std::cerr << "FeedbackAgent failed: " << feedback.value("message", "unknown") << std::endl;
    }

    std::cout << "Generating interface..." << std::endl;

    json ig_input = {
        {"text_metrics", metrics.value("metrics", json::object())},
        {"feedback_analysis", feedback.value("analysis", json::object())},
        {"original_text", text}
    };
    json html_result = interface_gen.process_raw(ig_input);
    json html_data = html_result["payload"]["data"];

    if (html_data["status"] == "success") {
        write_file(output_path, html_data["html"].get<std::string>());
        std::cout << "Saved: " << output_path << " (" << html_data["html_size"] << " bytes)" << std::endl;
    } else {
        std::cerr << "InterfaceGenerator failed: " << html_data.value("message", "unknown") << std::endl;
        return 1;
    }

    return 0;
}
