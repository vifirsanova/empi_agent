/**
 * @file orchestrate_agents.cpp
 * @brief Parallel orchestration of TextAnalyzer, FeedbackAgent, and InterfaceGenerator
 */

#include "../src/agents/TextAnalyzer.hpp"
#include "../src/agents/FeedbackAgent.hpp"
#include "../src/agents/InterfaceGenerator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <chrono>
#include <iomanip>
#include <filesystem>

using namespace EMPI;
using json = nlohmann::json;
namespace fs = std::filesystem;

class OrchestrationLogger {
public:
    enum class Level { INFO, WARNING, ERROR, SUCCESS };
    
    OrchestrationLogger() {
        std::cout << "EMPI AGENT ORCHESTRATION\n";
        std::cout << "=======================\n\n";
    }
    
    void log(Level level, const std::string& agent, const std::string& message) {
        std::string prefix;
        switch (level) {
            case Level::INFO:    prefix = "[INFO] "; break;
            case Level::WARNING: prefix = "[WARN] "; break;
            case Level::ERROR:   prefix = "[ERR] ";  break;
            case Level::SUCCESS: prefix = "[OK] ";   break;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&time);
        time_str.pop_back();
        
        std::cout << time_str << " | " 
                  << std::setw(18) << std::left << agent 
                  << " | " << prefix << message << "\n";
    }
    
    void log_json(const std::string& label, const json& j) {
        std::cout << "\n--- " << label << " ---\n";
        std::cout << j.dump(2) << "\n";
        std::cout << "---\n\n";
    }
    
    void separator() {
        std::cout << "----------------------------------------\n";
    }
};

json generate_mock_dialog() {
    return json::array({
        {{"role", "user"}, {"content", "The interface is confusing. I can't find the settings."}},
        {{"role", "assistant"}, {"content", "I apologize for the confusion. The settings are in the top-right menu."}},
        {{"role", "user"}, {"content", "Found it, but it's too complicated. Can you make it simpler?"}},
        {{"role", "assistant"}, {"content", "Thank you for the feedback. We'll work on simplifying the interface."}}
    });
}

struct AgentResults {
    json text_analysis;
    json feedback_analysis;
    json interface_html;
    std::chrono::milliseconds text_time{0};
    std::chrono::milliseconds feedback_time{0};
    std::chrono::milliseconds interface_time{0};
};

AgentResults run_parallel_agents(OrchestrationLogger& logger, const std::string& model_path) {
    AgentResults results;
    auto overall_start = std::chrono::high_resolution_clock::now();
    
    logger.log(OrchestrationLogger::Level::INFO, "Main", "Initializing agents...");
    
    TextAnalyzer text_agent;
    FeedbackAgent feedback_agent(model_path);
    InterfaceGenerator interface_gen(model_path);
    
    logger.log(OrchestrationLogger::Level::INFO, "TextAnalyzer", 
               text_agent.is_available() ? "Available" : "Fallback mode");
    logger.log(OrchestrationLogger::Level::INFO, "FeedbackAgent", 
               feedback_agent.is_available() ? "Available" : "Fallback mode");
    logger.log(OrchestrationLogger::Level::INFO, "InterfaceGenerator", 
               interface_gen.is_available() ? "Available" : "Fallback mode");
    
    logger.separator();
    
    // Prepare inputs
    std::string sample_text = 
        "The user interface needs to be simplified. Users are complaining about "
        "the complexity of the settings menu. Feedback indicates that the navigation "
        "is not intuitive and the color scheme could be improved for better readability.";
    
    json dialog_history = generate_mock_dialog();
    
    json text_input = {{"text", sample_text}};
    json feedback_input = {{"dialog_history", dialog_history}};
    
    logger.log(OrchestrationLogger::Level::INFO, "Main", "Starting parallel execution...");
    
    // Run TextAnalyzer and FeedbackAgent in parallel
    auto text_future = std::async(std::launch::async, [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        json result = text_agent.process_raw(text_input);
        auto end = std::chrono::high_resolution_clock::now();
        results.text_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        return result;
    });
    
    auto feedback_future = std::async(std::launch::async, [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        json result = feedback_agent.process_raw(feedback_input, "feedback_analysis");
        auto end = std::chrono::high_resolution_clock::now();
        results.feedback_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        return result;
    });
    
    results.text_analysis = text_future.get();
    results.feedback_analysis = feedback_future.get();
    
    logger.log(OrchestrationLogger::Level::SUCCESS, "TextAnalyzer", 
               "Completed in " + std::to_string(results.text_time.count()) + "ms");
    logger.log(OrchestrationLogger::Level::SUCCESS, "FeedbackAgent", 
               "Completed in " + std::to_string(results.feedback_time.count()) + "ms");
    
    logger.separator();
    
    // Extract data for interface generation
    json text_data = results.text_analysis["payload"]["data"];
    json feedback_data = results.feedback_analysis["payload"]["data"];
    
    if (text_data["status"] == "success") {
        logger.log_json("Text Metrics", text_data["metrics"]);
    }
    
    if (feedback_data["status"] == "success") {
        logger.log_json("Feedback Analysis", feedback_data["analysis"]);
    }
    
    // Generate interface
    json interface_input = {
        {"text_metrics", text_data["metrics"]},
        {"feedback_analysis", feedback_data["analysis"]}
    };
    
    logger.log(OrchestrationLogger::Level::INFO, "Main", "Generating interface...");
    
    auto interface_start = std::chrono::high_resolution_clock::now();
    results.interface_html = interface_gen.process_raw(interface_input, "html_generation");
    auto interface_end = std::chrono::high_resolution_clock::now();
    results.interface_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        interface_end - interface_start);
    
    logger.log(OrchestrationLogger::Level::SUCCESS, "InterfaceGenerator", 
               "Generated in " + std::to_string(results.interface_time.count()) + "ms");
    
    json html_data = results.interface_html["payload"]["data"];
    if (html_data["status"] == "success") {
        std::string filename = "interface_" + 
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".html";
        std::ofstream file(filename);
        file << html_data["html"].get<std::string>();
        file.close();
        
        logger.log(OrchestrationLogger::Level::INFO, "Main", 
                   "HTML saved to: " + filename);
    }
    
    auto overall_end = std::chrono::high_resolution_clock::now();
    auto total = std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_start);
    
    logger.separator();
    logger.log(OrchestrationLogger::Level::INFO, "Main", 
               "Total time: " + std::to_string(total.count()) + "ms");
    
    return results;
}

int main(int argc, char** argv) {
    OrchestrationLogger logger;
    
    std::string model_path = "models/Phi-3-mini-4k-instruct-q4.gguf";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            model_path = argv[++i];
            break;
        }
    }
    
    if (!fs::exists(model_path)) {
        logger.log(OrchestrationLogger::Level::ERROR, "Main", 
                   "Model not found: " + model_path);
        logger.log(OrchestrationLogger::Level::INFO, "Main", 
                   "Using fallback mode without LLM");
    }
    
    try {
        run_parallel_agents(logger, model_path);
    } catch (const std::exception& e) {
        logger.log(OrchestrationLogger::Level::ERROR, "Main", e.what());
        return 1;
    }
    
    return 0;
}
