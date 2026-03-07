/**
 * @file test_orchestration.cpp
 * @brief Test script for running TextAnalyzer, FeedbackAgent, and InterfaceGenerator
 *        Processes all combinations of texts and dialogues (100x100)
 */

#include "../src/agents/TextAnalyzer.hpp"
#include "../src/agents/FeedbackAgent.hpp"
#include "../src/agents/InterfaceGenerator.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <future>
#include <filesystem>
#include <iomanip>

using namespace EMPI;
using json = nlohmann::json;

// Helper to load JSON from file
json load_json_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    json data;
    file >> data;
    return data;
}

// Helper to sanitize filename (extract ID from "text_0042" format)
std::string extract_id(const std::string& full_id) {
    // Assuming format like "text_0042" or "dialogue_0001"
    size_t underscore_pos = full_id.find('_');
    if (underscore_pos != std::string::npos) {
        return full_id.substr(underscore_pos + 1); // returns "0042"
    }
    return full_id; // fallback
}

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
    std::cout << "EMPI Agent Orchestration Test (Full Cross)\n";
    std::cout << "========================================\n\n";
    
    // Load test data from files
    std::cout << "Loading test data...\n";
    json texts_data, dialogs_data;
    
    try {
        texts_data = load_json_file("../tests/texts.json");
        dialogs_data = load_json_file("../tests/dialogs.json");
    } catch (const std::exception& e) {
        std::cerr << "Error loading data files: " << e.what() << "\n";
        return 1;
    }
    
    // Extract arrays
    auto texts = texts_data["texts"];
    auto dialogues = dialogs_data["dialogues"];
    
    size_t num_texts = texts.size();
    size_t num_dialogues = dialogues.size();
    
    std::cout << "Loaded " << num_texts << " texts and " << num_dialogues << " dialogues\n";
    std::cout << "Total combinations: " << num_texts * num_dialogues << "\n\n";
    
    // Create agents
    std::cout << "Initializing agents...\n";
    TextAnalyzer text_agent;
    FeedbackAgent feedback_agent(model_path);
    InterfaceGenerator interface_gen(model_path);
    
    std::cout << "TextAnalyzer: " << (text_agent.is_available() ? "available" : "fallback mode") << "\n";
    std::cout << "FeedbackAgent: " << (feedback_agent.is_available() ? "available" : "fallback mode") << "\n";
    std::cout << "InterfaceGenerator: " << (interface_gen.is_available() ? "available" : "fallback mode") << "\n\n";
    
    // Create output directory if it doesn't exist
    std::filesystem::create_directories("output");
    
    // Process all combinations
    size_t success_count = 0;
    size_t error_count = 0;
    
    auto start_time = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < num_texts; ++i) {
        const auto& text_item = texts[i];
        std::string text_id = text_item["id"];
        std::string text_content = text_item["content"];
        
        std::cout << "Processing text " << (i+1) << "/" << num_texts 
                  << " (ID: " << text_id << ")\n";
        
        for (size_t j = 0; j < num_dialogues; ++j) {
            const auto& dialogue_item = dialogues[j];
            std::string dialogue_id = dialogue_item["id"];
            json dialog_history = dialogue_item["history"];
            
            // Progress indicator
            if ((j + 1) % 10 == 0 || j == 0) {
                std::cout << "  dialogue " << (j+1) << "/" << num_dialogues << "\r";
                std::cout.flush();
            }
            
            try {
                // Run TextAnalyzer and FeedbackAgent in parallel
                auto text_future = std::async(std::launch::async, [&]() {
                    json input = {{"text", text_content}};
                    return text_agent.process_raw(input);
                });
                
                auto feedback_future = std::async(std::launch::async, [&]() {
                    json input = {{"dialog_history", dialog_history}};
                    return feedback_agent.process_raw(input, "feedback_analysis");
                });
                
                // Wait for both
                json text_result = text_future.get();
                json feedback_result = feedback_future.get();
                
                // Extract data
                json text_data = text_result["payload"]["data"];
                json feedback_data = feedback_result["payload"]["data"];
                
                if (text_data["status"] != "success" || feedback_data["status"] != "success") {
                    throw std::runtime_error("Agent processing failed");
                }
                
                // Generate interface
                json interface_input = {
                    {"text_metrics", text_data["metrics"]},
                    {"feedback_analysis", feedback_data["analysis"]}
                };
                
                json interface_result = interface_gen.process_raw(interface_input, "html_generation");
                json interface_data = interface_result["payload"]["data"];
                
                if (interface_data["status"] != "success") {
                    throw std::runtime_error("Interface generation failed");
                }
                
                // Save HTML with named template
                std::string html = interface_data["html"];
                
                // Extract numeric parts for clean filename
                std::string text_num = extract_id(text_id);
                std::string dial_num = extract_id(dialogue_id);
                
                std::string filename = "output/template_" + text_num + "_" + dial_num + ".html";
                
                std::ofstream file(filename);
                file << html;
                file.close();
                
                success_count++;
                
            } catch (const std::exception& e) {
                error_count++;
                // Optional: log errors quietly, or print if debugging
                // std::cerr << "Error on text " << text_id << ", dialogue " << dialogue_id 
                //           << ": " << e.what() << "\n";
            }
        }
        std::cout << "\n";
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\n========================================\n";
    std::cout << "Processing complete\n";
    std::cout << "Successful: " << success_count << "\n";
    std::cout << "Failed: " << error_count << "\n";
    std::cout << "Time: " << elapsed.count() << " seconds\n";
    std::cout << "HTML files saved in 'output/' directory\n";
    
    return 0;
}
