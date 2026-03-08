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
#include <unordered_map>

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

// Helper to load existing cache from file
json load_cache_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return json::object(); // Return empty object if file doesn't exist
    }
    try {
        json cache;
        file >> cache;
        return cache;
    } catch (...) {
        return json::object(); // Return empty object on parse error
    }
}

// Helper to save cache to file
void save_cache_to_file(const json& cache, const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << cache.dump(2);
        file.close();
    }
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
    std::cout << "EMPI Agent Orchestration Test (Optimized)\n";
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
    
    // Cache for feedback results
    std::unordered_map<std::string, json> feedback_cache;
    
    // Load existing cache if available
    std::string cache_file = "output/feedback_cache.json";
    json persistent_cache = load_cache_from_file(cache_file);
    std::cout << "Loaded " << persistent_cache.size() << " cached feedback entries\n\n";
    
    // STEP 1: Pre-process all dialogues (only 100 calls to FeedbackAgent)
    std::cout << "\n========================================\n";
    std::cout << "STEP 1: Pre-processing dialogues (caching phase)\n";
    std::cout << "========================================\n";
    
    size_t feedback_success = persistent_cache.size(); // Start with cached count
    size_t feedback_errors = 0;
    size_t feedback_skipped = 0;
    
    // First, populate cache with existing entries
    for (auto& [dialogue_id, analysis] : persistent_cache.items()) {
        feedback_cache[dialogue_id] = analysis;
    }
    
    for (size_t j = 0; j < num_dialogues; ++j) {
        const auto& dialogue_item = dialogues[j];
        std::string dialogue_id = dialogue_item["id"];
        json dialog_history = dialogue_item["history"];
        
        // Check if already in cache
        if (feedback_cache.find(dialogue_id) != feedback_cache.end()) {
            feedback_skipped++;
            std::cout << "\n>>> Skipping dialogue " << (j+1) << "/" << num_dialogues 
                      << " (ID: " << dialogue_id << ") - already cached\n";
            continue;
        }
        
        std::cout << "\n>>> Processing dialogue " << (j+1) << "/" << num_dialogues 
                  << " (ID: " << dialogue_id << ")\n";
        
        try {
            json input = {{"dialog_history", dialog_history}};
            auto result = feedback_agent.process_raw(input, "feedback_analysis");
            json feedback_data = result["payload"]["data"];
            
            if (feedback_data["status"] == "success") {
                json analysis = feedback_data["analysis"];
                feedback_cache[dialogue_id] = analysis;
                persistent_cache[dialogue_id] = analysis; // Update persistent cache
                feedback_success++;
                std::cout << "    [OK] Feedback cached for " << dialogue_id << "\n";
            } else {
                feedback_errors++;
                std::cerr << "    [ERROR] FeedbackAgent failed for " << dialogue_id 
                          << ": " << feedback_data.value("message", "unknown") << "\n";
            }
        } catch (const std::exception& e) {
            feedback_errors++;
            std::cerr << "    [ERROR] Exception on " << dialogue_id << ": " << e.what() << "\n";
        }
    }
    
    // Save updated cache to file
    save_cache_to_file(persistent_cache, cache_file);
    
    std::cout << "\nFeedback caching complete:\n";
    std::cout << "  Already cached (skipped): " << feedback_skipped << "\n";
    std::cout << "  Newly cached: " << (feedback_success - persistent_cache.size() + feedback_skipped) << "\n";
    std::cout << "  Total successful: " << feedback_success << "\n";
    std::cout << "  Failed: " << feedback_errors << "\n";
    std::cout << "  Cache saved to: " << cache_file << "\n";
    
    // STEP 2: Generate interfaces for all combinations
    std::cout << "\n========================================\n";
    std::cout << "STEP 2: Generating interfaces (using cached feedback)\n";
    std::cout << "========================================\n";
    
    size_t interface_success = 0;
    size_t interface_errors = 0;
    size_t interface_skipped = 0;
    
    // Check for existing HTML files
    std::unordered_map<std::string, bool> existing_html;
    for (const auto& entry : std::filesystem::directory_iterator("output")) {
        if (entry.path().extension() == ".html") {
            existing_html[entry.path().filename().string()] = true;
        }
    }
    std::cout << "Found " << existing_html.size() << " existing HTML files\n";
    
    auto start_time = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < num_texts; ++i) {
        const auto& text_item = texts[i];
        std::string text_id = text_item["id"];
        std::string text_content = text_item["content"];
        
        std::cout << "\n>>> Processing text " << (i+1) << "/" << num_texts 
                  << " (ID: " << text_id << ")\n";
        std::cout << ">>> Text preview: " << text_content.substr(0, 50) << "...\n";
        
        // Analyze text once per text
        json text_metrics;
        try {
            json input = {{"text", text_content}};
            auto text_result = text_agent.process_raw(input);
            json text_data = text_result["payload"]["data"];
            
            if (text_data["status"] != "success") {
                throw std::runtime_error("Text analysis failed: " + 
                    text_data.value("message", "unknown"));
            }
            text_metrics = text_data["metrics"];
            std::cout << "    [TextAnalyzer] metrics: " << text_metrics.dump(2) << "\n";
        } catch (const std::exception& e) {
            std::cerr << "    [ERROR] Text analysis failed for " << text_id << ": " << e.what() << "\n";
            interface_errors += num_dialogues; // Skip all combinations for this text
            continue;
        }
        
        // Generate interface for each dialogue
        for (size_t j = 0; j < num_dialogues; ++j) {
            const auto& dialogue_item = dialogues[j];
            std::string dialogue_id = dialogue_item["id"];
            
            // Check if HTML already exists
            std::string text_num = extract_id(text_id);
            std::string dial_num = extract_id(dialogue_id);
            std::string filename = "output/template_" + text_num + "_" + dial_num + ".html";
            
            if (std::filesystem::exists(filename)) {
                interface_skipped++;
                if (interface_skipped % 100 == 0) {
                    std::cout << "  Skipped " << interface_skipped << " existing files\r";
                    std::cout.flush();
                }
                continue;
            }
            
            // Progress indicator
            if ((j + 1) % 10 == 0 || j == 0) {
                std::cout << "  dialogue " << (j+1) << "/" << num_dialogues << "\r";
                std::cout.flush();
            }
            
            // Skip if feedback not cached
            auto cache_it = feedback_cache.find(dialogue_id);
            if (cache_it == feedback_cache.end()) {
                interface_errors++;
                continue;
            }
            
            try {
                // Generate interface using cached feedback
                json interface_input = {
                    {"text_metrics", text_metrics},
                    {"feedback_analysis", cache_it->second},
                    {"original_text", text_content}
                };
                
                json interface_result = interface_gen.process_raw(interface_input, "html_generation");
                json interface_data = interface_result["payload"]["data"];
                
                if (interface_data["status"] != "success") {
                    throw std::runtime_error("Interface generation failed: " + 
                        interface_data.value("message", "unknown"));
                }
                
                // Save HTML
                std::string html = interface_data["html"];
                
                std::ofstream file(filename);
                file << html;
                file.close();
                
                interface_success++;
                
            } catch (const std::exception& e) {
                interface_errors++;
                if (interface_errors < 10) { // Limit error spam
                    std::cerr << "\n    [ERROR] on " << text_id << " x " << dialogue_id 
                              << ": " << e.what() << "\n";
                }
            }
        }
        std::cout << "\n  Completed text " << text_id << "\n";
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\n========================================\n";
    std::cout << "Processing complete\n";
    std::cout << "Feedback cached: " << feedback_success << " / " << num_dialogues << "\n";
    std::cout << "Interfaces generated: " << interface_success << " / " << num_texts * num_dialogues << "\n";
    std::cout << "Interfaces skipped (already exist): " << interface_skipped << "\n";
    std::cout << "Interface errors: " << interface_errors << "\n";
    std::cout << "Time: " << elapsed.count() << " seconds\n";
    std::cout << "HTML files saved in 'output/' directory\n";
    std::cout << "Feedback cache saved in 'output/feedback_cache.json'\n";
    
    return 0;
}
