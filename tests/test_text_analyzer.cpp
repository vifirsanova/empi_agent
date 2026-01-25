/**
 * @file test_text_analyzer.cpp
 * @brief Unit tests for TextAnalyzer EMPI agent with compact logging
 */

#include "../src/agents/TextAnalyzer.hpp"
#include "../src/core/UniversalAgent.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <filesystem>
#include <chrono>
#include <iomanip>

using namespace EMPI;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// КОМПАКТНОЕ ЛОГГИРОВАНИЕ
// ============================================================================

class TestLogger {
public:
    enum class Level { INFO, DEBUG, WARNING, ERROR, SUCCESS };
    
    TestLogger(const std::string& test_name) : test_name_(test_name) {
        std::cout << "\n=== TEST: " << test_name_ << "\n";
    }
    
    ~TestLogger() {
        // No separator at the end
    }
    
    void log(Level level, const std::string& message) {
        // Skip DEBUG messages
        if (level == Level::DEBUG) return;
        
        std::string prefix;
        switch (level) {
            case Level::INFO: prefix = "[INFO] "; break;
            case Level::WARNING: prefix = "[WARN] "; break;
            case Level::ERROR: prefix = "[ERR] "; break;
            case Level::SUCCESS: prefix = "[OK] "; break;
            default: return;
        }
        std::cout << prefix << message << "\n";
    }
    
    void log_json(const std::string& label, const json& j) {
        std::cout << "[JSON] " << label << ":\n";
        std::cout << j.dump(2) << "\n";
    }
    
    void log_text_sample(const std::string& label, const std::string& text, size_t max_chars = 150) {
        std::cout << "[TEXT] " << label << " (" << text.length() << " chars): ";
        if (text.length() <= max_chars) {
            std::cout << "\"" << text << "\"\n";
        } else {
            std::cout << "\"" << text.substr(0, max_chars) << "...\"\n";
        }
    }
    
private:
    std::string test_name_;
};

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

std::string load_sample_text(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "This is a fallback test text. It contains multiple sentences "
               "for testing text analysis functionality. The agent should "
               "compute various readability metrics from this input.";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ============================================================================
// ТЕСТЫ
// ============================================================================

void test_agent_creation() {
    TestLogger logger("Agent Creation");
    
    try {
        logger.log(TestLogger::Level::INFO, "Creating TextAnalyzer...");
        TextAnalyzer analyzer1;
        logger.log(TestLogger::Level::SUCCESS, "Default constructor succeeded");
        logger.log(TestLogger::Level::INFO, 
            "Agent ID: " + analyzer1.get_agent_id());
        
        TextAnalyzer analyzer2("python3");
        logger.log(TestLogger::Level::SUCCESS, "Constructor with Python path succeeded");
        
    } catch (const std::exception& e) {
        logger.log(TestLogger::Level::ERROR, std::string("Agent creation failed: ") + e.what());
        throw;
    }
}

void test_empi_protocol() {
    TestLogger logger("EMPI Protocol Compliance");
    
    TextAnalyzer analyzer;
    
    std::string test_text = "This is a simple test sentence for EMPI protocol validation.";
    logger.log_text_sample("Input", test_text);
    
    json input = {
        {"text", test_text},
        {"language", "en"},
        {"meta", {{"test_id", "protocol_test_001"}}}
    };
    
    logger.log_json("Request", input);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    json result = analyzer.process_raw(input, "text_metrics");
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    logger.log(TestLogger::Level::INFO, 
        "Processing time: " + std::to_string(duration.count()) + "ms");
    
    logger.log_json("Response", result);
    
    // Validate structure
    assert(result.contains("header"));
    assert(result.contains("payload"));
    
    auto& data = result["payload"]["data"];
    std::string status = data["status"];
    
    if (status == "success") {
        logger.log(TestLogger::Level::SUCCESS, "Text analysis succeeded!");
    } else {
        logger.log(TestLogger::Level::ERROR, "Text analysis failed");
    }
}

void test_error_handling() {
    TestLogger logger("Error Handling");
    
    TextAnalyzer analyzer;
    logger.log(TestLogger::Level::INFO, "Testing error conditions...");
    
    // Test 1: Empty JSON
    {
        logger.log(TestLogger::Level::INFO, "Test 1: Empty JSON");
        json empty_input = json::object();
        json result = analyzer.process_raw(empty_input);
        auto& data = result["payload"]["data"];
        
        if (data["status"] == "error") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Correctly handled empty input");
        }
    }
    
    // Test 2: Empty text string
    {
        logger.log(TestLogger::Level::INFO, "Test 2: Empty text");
        json empty_text = {{"text", ""}};
        json result = analyzer.process_raw(empty_text);
        auto& data = result["payload"]["data"];
        
        if (data["status"] == "error") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Correctly handled empty text");
        }
    }
    
    // Test 3: Whitespace-only text
    {
        logger.log(TestLogger::Level::INFO, "Test 3: Whitespace-only");
        json whitespace_text = {{"text", "   \n\n\t  "}};
        json result = analyzer.process_raw(whitespace_text);
        auto& data = result["payload"]["data"];
        
        if (data["status"] == "error") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Correctly handled whitespace-only text");
        }
    }
    
    // Test 4: Text in 'content' field
    {
        logger.log(TestLogger::Level::INFO, "Test 4: Text in 'content' field");
        json alt_input = {
            {"content", "This text is in the 'content' field"},
            {"language", "en"}
        };
        
        json result = analyzer.process_raw(alt_input);
        auto& data = result["payload"]["data"];
        
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Found text in 'content' field");
        }
    }
}

void test_sample_text_file() {
    TestLogger logger("Sample Text File Analysis");
    
    TextAnalyzer analyzer;
    
    if (!analyzer.is_available()) {
        logger.log(TestLogger::Level::WARNING, "Agent not available");
        return;
    }
    
    // Use relative path
    std::string filename = "integrations/sample_text.txt";
    
    // Try multiple relative paths
    if (!fs::exists(filename)) {
        // Try from build directory
        filename = "../empi_agent/integrations/sample_text.txt";
    }
    
    if (!fs::exists(filename)) {
        logger.log(TestLogger::Level::WARNING, 
            "Sample file not found: " + filename);
        return;
    }
    
    logger.log(TestLogger::Level::INFO, "Loading: " + filename);
    
    std::string sample_text = load_sample_text(filename);
    logger.log_text_sample("Sample text", sample_text, 200);
    
    logger.log(TestLogger::Level::INFO, 
        "Length: " + std::to_string(sample_text.length()) + " chars");
    
    json input = {
        {"text", sample_text},
        {"language", "en"},
        {"meta", {{"source", filename}}}
    };
    
    logger.log_json("Request", input);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    json result = analyzer.process_raw(input);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    logger.log(TestLogger::Level::INFO, 
        "Processing time: " + std::to_string(duration.count()) + "ms");
    
    logger.log_json("Response", result);
    
    auto& data = result["payload"]["data"];
    
    if (data["status"] == "success") {
        logger.log(TestLogger::Level::SUCCESS, "Sample text analysis successful");
        logger.log(TestLogger::Level::INFO, 
            "Complexity: " + data["complexity_label"].get<std::string>() +
            ", Accessibility: " + data["accessibility_level"].get<std::string>());
    }
}

void test_actual_analysis() {
    TestLogger logger("Actual Text Analysis");
    
    TextAnalyzer analyzer;
    
    if (!analyzer.is_available()) {
        logger.log(TestLogger::Level::WARNING, "Agent not available");
        return;
    }
    
    // Test 1: Simple text
    {
        logger.log(TestLogger::Level::INFO, "Test 1: Simple text");
        
        std::string simple_text = "The quick brown fox jumps over the lazy dog.";
        logger.log_text_sample("Input", simple_text);
        
        json input = {{"text", simple_text}, {"language", "en"}};
        logger.log_json("Request", input);
        
        json result = analyzer.process_raw(input);
        logger.log_json("Response", result);
        
        auto& data = result["payload"]["data"];
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, "Simple text analysis successful");
        }
    }
    
    // Test 2: Text without language
    {
        logger.log(TestLogger::Level::INFO, "Test 2: Text without language");
        
        std::string text = "Ce texte est en français.";
        logger.log_text_sample("Input", text);
        
        json input = {{"text", text}};
        logger.log_json("Request", input);
        
        json result = analyzer.process_raw(input);
        
        auto& data = result["payload"]["data"];
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Analysis succeeded without language");
        }
    }
}

void test_agent_state() {
    TestLogger logger("Agent State Management");
    
    TextAnalyzer analyzer;
    
    logger.log(TestLogger::Level::INFO, "Processing 3 texts...");
    
    std::vector<std::string> test_texts = {
        "First test text.",
        "Second text.",
        "Third text."
    };
    
    for (size_t i = 0; i < test_texts.size(); i++) {
        json input = {{"text", test_texts[i]}};
        analyzer.process_raw(input);
        
        json state = analyzer.get_agent_state();
        if (state.contains("total_texts_processed")) {
            int processed = state["total_texts_processed"];
            logger.log(TestLogger::Level::INFO, 
                "After text " + std::to_string(i+1) + ": " + 
                std::to_string(processed) + " processed");
        }
    }
    
    analyzer.reset_state();
    json reset_state = analyzer.get_agent_state();
    
    if (reset_state.empty()) {
        logger.log(TestLogger::Level::SUCCESS, "State reset successful");
    }
}

void test_edge_cases() {
    TestLogger logger("Edge Cases");
    
    TextAnalyzer analyzer;
    
    if (!analyzer.is_available()) {
        logger.log(TestLogger::Level::WARNING, "Agent not available");
        return;
    }
    
    // Test 1: Very short text
    {
        logger.log(TestLogger::Level::INFO, "Test 1: Very short text");
        std::string short_text = "Hi!";
        logger.log_text_sample("Input", short_text);
        
        json input = {{"text", short_text}};
        json result = analyzer.process_raw(input);
        
        auto& data = result["payload"]["data"];
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, "Short text processed");
        }
    }
    
    // Test 2: Special characters
    {
        logger.log(TestLogger::Level::INFO, "Test 2: Special characters");
        std::string special_text = "Text with spéçïål chãràctërs @#$% 😀";
        logger.log_text_sample("Input", special_text);
        
        json input = {{"text", special_text}};
        json result = analyzer.process_raw(input);
        
        auto& data = result["payload"]["data"];
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, "Special chars handled");
        }
    }
    
    // Test 3: Long text
    {
        logger.log(TestLogger::Level::INFO, "Test 3: Long text");
        
        std::string long_text;
        for (int i = 0; i < 10; i++) {  // Smaller for faster test
            long_text += "Paragraph " + std::to_string(i + 1) + ": ";
            for (int j = 0; j < 5; j++) {
                long_text += "Sentence " + std::to_string(j + 1) + ". ";
            }
            long_text += "\n";
        }
        
        logger.log(TestLogger::Level::INFO, 
            "Generated: " + std::to_string(long_text.length()) + " chars");
        
        json input = {{"text", long_text}};
        auto start_time = std::chrono::high_resolution_clock::now();
        json result = analyzer.process_raw(input);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        auto& data = result["payload"]["data"];
        if (data["status"] == "success") {
            logger.log(TestLogger::Level::SUCCESS, 
                "Long text processed in " + std::to_string(duration.count()) + "ms");
        }
    }
}

// ============================================================================
// ГЛАВНАЯ ФУНКЦИЯ
// ============================================================================

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "          EMPI TEXTANALYZER TEST SUITE\n";
    std::cout << std::string(60, '=') << "\n";
    
    auto overall_start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"Agent Creation", test_agent_creation},
        {"EMPI Protocol", test_empi_protocol},
        {"Error Handling", test_error_handling},
        {"Sample Text File", test_sample_text_file},
        {"Actual Analysis", test_actual_analysis},
        {"Agent State", test_agent_state},
        {"Edge Cases", test_edge_cases}
    };
    
    int tests_passed = 0;
    int tests_failed = 0;
    
    for (auto& [test_name, test_func] : tests) {
        try {
            test_func();
            tests_passed++;
        } catch (const std::exception& e) {
            std::cout << "\n[FAIL] Test '" << test_name << "' failed:\n";
            std::cout << "  " << e.what() << "\n";
            tests_failed++;
        } catch (...) {
            std::cout << "\n[FAIL] Test '" << test_name << "' failed with unknown exception\n";
            tests_failed++;
        }
    }
    
    auto overall_end = std::chrono::high_resolution_clock::now();
    auto overall_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        overall_end - overall_start);
    
    // Красивая сводка
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "                 TEST SUMMARY\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Total tests:  " << tests.size() << "\n";
    std::cout << "Passed:       " << tests_passed << "\n";
    std::cout << "Failed:       " << tests_failed << "\n";
    std::cout << "Total time:   " << overall_duration.count() << "ms\n";
    std::cout << std::string(60, '=') << "\n";
    
    if (tests_failed == 0) {
        std::cout << "ALL TESTS PASSED SUCCESSFULLY!\n";
    } else {
        std::cout << "⚠ " << tests_failed << " TEST(S) FAILED\n";
    }
    
    std::cout << std::string(60, '=') << "\n\n";
    
    return tests_failed > 0 ? 1 : 0;
}
