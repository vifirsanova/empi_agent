/**
 * @file test_orchestration.cpp
 * @brief Test script for running TextAnalyzer, FeedbackAgent, and InterfaceGenerator
 */

#include "../src/agents/TextAnalyzer.hpp"
#include "../src/agents/FeedbackAgent.hpp"
#include "../src/agents/InterfaceGenerator.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <future>

using namespace EMPI;
using json = nlohmann::json;

int main(int argc, char** argv) {
    std::string model_path = "models/Phi-3-mini-4k-instruct-q4.gguf";
    
    // Parse command line for model path
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            model_path = argv[++i];
            break;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "EMPI Agent Orchestration Test\n";
    std::cout << "========================================\n\n";
    
    // Create agents
    std::cout << "Initializing agents...\n";
    TextAnalyzer text_agent;
    FeedbackAgent feedback_agent(model_path);
    InterfaceGenerator interface_gen(model_path);
    
    std::cout << "TextAnalyzer: " << (text_agent.is_available() ? "available" : "fallback mode") << "\n";
    std::cout << "FeedbackAgent: " << (feedback_agent.is_available() ? "available" : "fallback mode") << "\n";
    std::cout << "InterfaceGenerator: " << (interface_gen.is_available() ? "available" : "fallback mode") << "\n\n";
    
    // Test data
    std::string sample_text = 
        "The user interface needs to be simplified. Users are complaining about "
        "the complexity of the settings menu. Feedback indicates that the navigation "
        "is not intuitive and the color scheme could be improved for better readability.";
    
    json dialog_history = json::array({
        {{"role", "user"}, {"content", "The interface is confusing. I can't find the settings."}},
        {{"role", "assistant"}, {"content", "Settings are in the top-right menu."}},
        {{"role", "user"}, {"content", "Found it, but it's too complicated. Can you make it simpler?"}},
        {{"role", "assistant"}, {"content", "Thank you for the feedback."}}
    });
    
    // Run TextAnalyzer and FeedbackAgent in parallel
    std::cout << "Running TextAnalyzer and FeedbackAgent in parallel...\n";
    
    auto text_future = std::async(std::launch::async, [&]() {
        json input = {{"text", sample_text}};
        return text_agent.process_raw(input);
    });
    
    auto feedback_future = std::async(std::launch::async, [&]() {
        json input = {{"dialog_history", dialog_history}};
        return feedback_agent.process_raw(input, "feedback_analysis");
    });
    
    // Wait for both
    json text_result = text_future.get();
    json feedback_result = feedback_future.get();
    
    std::cout << "TextAnalyzer complete\n";
    std::cout << "FeedbackAgent complete\n\n";
    
    // Extract data
    json text_data = text_result["payload"]["data"];
    json feedback_data = feedback_result["payload"]["data"];
    
    if (text_data["status"] == "success") {
        std::cout << "Text Metrics:\n";
        std::cout << text_data["metrics"].dump(2) << "\n\n";
    } else {
        std::cout << "Text analysis failed: " << text_data.value("message", "unknown error") << "\n\n";
    }
    
    if (feedback_data["status"] == "success") {
        std::cout << "Feedback Analysis:\n";
        std::cout << feedback_data["analysis"].dump(2) << "\n\n";
    } else {
        std::cout << "Feedback analysis failed: " << feedback_data.value("message", "unknown error") << "\n\n";
    }
    
    // Generate interface
    std::cout << "Generating interface...\n";
    
    json interface_input = {
        {"text_metrics", text_data["metrics"]},
        {"feedback_analysis", feedback_data["analysis"]}
    };
    
    json interface_result = interface_gen.process_raw(interface_input, "html_generation");
    json interface_data = interface_result["payload"]["data"];
    
    if (interface_data["status"] == "success") {
        std::string html = interface_data["html"];
        std::string filename = "interface_output.html";
        
        std::ofstream file(filename);
        file << html;
        file.close();
        
        std::cout << "HTML generated: " << html.length() << " bytes\n";
        std::cout << "Saved to: " << filename << "\n";
    } else {
        std::cout << "Interface generation failed: " << interface_data.value("message", "unknown error") << "\n";
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Test complete\n";
    
    return 0;
}
