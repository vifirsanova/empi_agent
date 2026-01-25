/**
 * @file TextAnalyzer.cpp
 * @brief Implementation of TextAnalyzer agent for text analysis via Python NLP
 */

#include "TextAnalyzer.hpp"
#include <string>
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <unistd.h>  // For mkstemp, close, unlink
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

/**
 * @class TextAnalyzer::PythonSubprocessImpl
 * @brief Executes Python scripts via subprocess pipes.
 * 
 * Handles:
 * - Python interpreter discovery
 * - JSON data exchange via stdin/stdout
 * - Process lifecycle management
 */
class TextAnalyzer::PythonSubprocessImpl {
public:
    /**
     * @brief Initializes Python subprocess handler.
     * 
     * @param python_path Optional Python interpreter path
     * @throws std::runtime_error If Python or script not found
     */
    PythonSubprocessImpl(const std::string& python_path)
        : python_path_(find_python_executable(python_path))
        , script_path_("integrations/text_analyzer.py") 
    {
        validate_environment();
    }
    
    /**
     * @brief Executes Python script with JSON input.
     * 
     * Protocol:
     * 1. Write JSON to temporary file
     * 2. Call Python script with file path
     * 3. Read JSON from temporary output file
     * 
     * @param input_data JSON to pass to script
     * @return JSON containing script output
     */
    json call_script_with_json_input(const json& input_data) {
        char temp_input[256] = "/tmp/text_analyzer_input_XXXXXX";
        char temp_output[256] = "/tmp/text_analyzer_output_XXXXXX";
        int fd_input = -1, fd_output = -1;
        
        try {
            // Create temporary input file
            fd_input = mkstemp(temp_input);
            if (fd_input == -1) {
                throw std::runtime_error("Failed to create temporary input file");
            }
            
            // Create temporary output file
            fd_output = mkstemp(temp_output);
            if (fd_output == -1) {
                close(fd_input);
                throw std::runtime_error("Failed to create temporary output file");
            }
            close(fd_output); // Close it, Python will reopen for writing
            
            // Write input JSON to temp file
            std::string input_json = input_data.dump();
            if (write(fd_input, input_json.c_str(), input_json.size()) != static_cast<ssize_t>(input_json.size())) {
                close(fd_input);
                unlink(temp_input);
                throw std::runtime_error("Failed to write to temporary file");
            }
            close(fd_input);
            
            // Build Python command that reads from input file and writes to output file
            std::string command = python_path_ + 
                " -c \""
                "import sys, json, os\n"
                "sys.path.insert(0, os.path.dirname('" + script_path_ + "'))\n"
                "with open('" + std::string(temp_input) + "', 'r') as f:\n"
                "    data = json.load(f)\n"
                "text = data.get('text', '')\n"
                "if not text:\n"
                "    result = {'error': 'No text provided in JSON'}\n"
                "else:\n"
                "    try:\n"
                "        from text_analyzer import TextAnalyzer\n"
                "        analyzer = TextAnalyzer()\n"
                "        result = analyzer.analyze(text)\n"
                "    except Exception as e:\n"
                "        result = {'error': 'Python analysis failed: ' + str(e)}\n"
                "with open('" + std::string(temp_output) + "', 'w') as f:\n"
                "    json.dump(result, f, ensure_ascii=False)\n"
                "\"";
            
            // Execute Python command
            int return_code = system(command.c_str());
            
            if (return_code != 0) {
                throw std::runtime_error("Python script failed with exit code: " + std::to_string(return_code));
            }
            
            // Read output from temporary file
            FILE* output_file = fopen(temp_output, "r");
            if (!output_file) {
                throw std::runtime_error("Failed to open output file");
            }
            
            std::array<char, 4096> buffer;
            std::string result;
            while (fgets(buffer.data(), buffer.size(), output_file) != nullptr) {
                result += buffer.data();
            }
            fclose(output_file);
            
            if (result.empty()) {
                throw std::runtime_error("Python script returned empty response");
            }
            
            // Clean up temporary files
            unlink(temp_input);
            unlink(temp_output);
            
            return json::parse(result);
            
        } catch (const std::exception& e) {
            // Clean up on error
            if (fd_input != -1) {
                close(fd_input);
                unlink(temp_input);
            }
            if (fd_output != -1) {
                unlink(temp_output);
            }
            return {{"error", std::string("Python subprocess error: ") + e.what()}};
        }
    }
    
    bool check_availability() const {
        return is_python_available() && script_exists();
    }
    
    std::string get_script_path() const { return script_path_; }
    std::string get_python_path() const { return python_path_; }
    
private:
    std::string python_path_;
    std::string script_path_;
    
    std::string find_python_executable(const std::string& preferred_path) const {
        if (!preferred_path.empty() && check_command(preferred_path + " --version")) {
            return preferred_path;
        }
        
        const std::vector<std::string> candidates = {
            "python3", "python", 
            "python3.11", "python3.10", "python3.9", "python3.8",
            #ifdef _WIN32
                "py", "python.exe"
            #endif
        };
        
        for (const auto& candidate : candidates) {
            if (check_command(candidate + " --version")) {
                return candidate;
            }
        }
        
        throw std::runtime_error("No Python interpreter found. Please install Python 3.8+");
    }
    
    bool script_exists() const {
        return fs::exists(script_path_) && fs::is_regular_file(script_path_);
    }
    
    bool check_command(const std::string& command) const {
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return false;
        
        char buffer[128];
        bool has_output = fgets(buffer, sizeof(buffer), pipe) != nullptr;
        pclose(pipe);
        
        return has_output;
    }
    
    bool is_python_available() const {
        return check_command(python_path_ + " --version");
    }
    
    void validate_environment() const {
        if (!is_python_available()) {
            throw std::runtime_error("Python interpreter '" + python_path_ + "' is not available");
        }
        
        if (!script_exists()) {
            throw std::runtime_error(
                "Python script not found at: " + script_path_ + "\n" +
                "Expected location: ./integrations/text_analyzer.py\n" +
                "Current working directory: " + fs::current_path().string()
            );
        }
    }
};

/**
 * @brief Constructs a text analysis agent.
 * 
 * @param python_path Preferred path to Python interpreter.
 *                    If empty, automatically searches for Python 3.8+.
 * @throws std::runtime_error If Python or script is not found.
 */
TextAnalyzer::TextAnalyzer(const std::string& python_path)
    : UniversalAgent("text_analyzer", "text_metrics")
    , python_impl_(std::make_unique<PythonSubprocessImpl>(python_path))
    , last_error_("")
{
    register_handlers();
}

TextAnalyzer::~TextAnalyzer() = default;

bool TextAnalyzer::is_available() const {
    if (!python_impl_) return false;
    try {
        return python_impl_->check_availability();
    } catch (...) {
        return false;
    }
}

std::string TextAnalyzer::get_last_error() const {
    return last_error_;
}

std::string TextAnalyzer::get_script_path() const {
    return python_impl_ ? python_impl_->get_script_path() : "";
}

std::string TextAnalyzer::get_python_path() const {
    return python_impl_ ? python_impl_->get_python_path() : "";
}

/**
 * @brief Registers EMPI protocol handlers.
 * 
 * φ-function: Extracts text and language from input
 * ψ-function: Calls Python for analysis, returns data field
 */
void TextAnalyzer::register_handlers() {
    register_handler("text_metrics",
        // φ-function
        [](const json& input, const json& context, json& state) -> json {
            json extracted_info;
            
            // Extract text with fallback hierarchy
            std::string text;
            if (input.contains("text")) {
                text = input["text"].get<std::string>();
            } else if (input.contains("content")) {
                text = input["content"].get<std::string>();
            } else if (input.contains("data") && input["data"].contains("text")) {
                text = input["data"]["text"].get<std::string>();
            }
            
            if (text.empty()) {
                extracted_info["error"] = "No text found in input. Expected fields: 'text', 'content', or 'data.text'";
                return extracted_info;
            }
            
            // Extract language
            if (input.contains("language")) {
                extracted_info["language"] = input["language"].get<std::string>();
            } else if (input.contains("meta") && input["meta"].contains("language")) {
                extracted_info["language"] = input["meta"]["language"];
            }
            
            extracted_info["text"] = text;
            
            // Update state
            state["total_texts_processed"] = 
                state.value("total_texts_processed", 0) + 1;
            state["total_chars_processed"] = 
                state.value("total_chars_processed", 0) + text.length();
            
            return extracted_info;
        },
        
        // ψ-function
        [this](const json& extracted_info, const json& context, json& state) -> json {
            json data_field;
            
            try {
                // Check φ-function errors
                if (extracted_info.contains("error")) {
                    data_field["status"] = "error";
                    data_field["message"] = extracted_info["error"];
                    data_field["error_type"] = "input_validation";
                    return data_field;
                }
                
                // Call Python script with known input structure
                json python_input;
                
                // Parse with known structure instead of contains()
                try {
                    python_input["text"] = extracted_info.at("text").get<std::string>();
                    
                    auto lang_it = extracted_info.find("language");
                    if (lang_it != extracted_info.end()) {
                        python_input["language"] = lang_it->get<std::string>();
                    }
                } catch (const json::exception& e) {
                    data_field["status"] = "error";
                    data_field["message"] = std::string("Invalid extracted info: ") + e.what();
                    data_field["error_type"] = "data_structure";
                    return data_field;
                }
                
                auto python_result = python_impl_->call_script_with_json_input(python_input);
                
                // Parse Python result with expected structure
                if (python_result.contains("error")) {
                    data_field["status"] = "error";
                    data_field["message"] = python_result["error"];
                    data_field["error_type"] = "python_script_error";
                    data_field["raw_python_output"] = python_result;
                    return data_field;
                }
                
                // Parse known metrics structure
                try {
                    // Required metrics
                    double flesch_kincaid = python_result.at("flesch_kincaid_grade").get<double>();
                    
                    data_field["status"] = "success";
                    data_field["analysis_id"] = 
                        "analyze_" + std::to_string(state.value("total_texts_processed", 0));
                    data_field["metrics"] = python_result;
                    
                    // Set complexity based on Flesch-Kincaid values
                    if (flesch_kincaid <= 8.0) {
                        data_field["complexity_label"] = "simple";
                        data_field["accessibility_level"] = "high";
                    } else if (flesch_kincaid <= 12.0) {
                        data_field["complexity_label"] = "moderate";
                        data_field["accessibility_level"] = "medium";
                    } else {
                        data_field["complexity_label"] = "complex";
                        data_field["accessibility_level"] = "low";
                    }
                    
                } catch (const json::exception& e) {
                    data_field["status"] = "error";
                    data_field["message"] = std::string("Invalid Python output structure: ") + e.what();
                    data_field["error_type"] = "output_structure";
                    data_field["raw_python_output"] = python_result;
                    return data_field;
                }
                
            } catch (const std::exception& e) {
                data_field["status"] = "error";
                data_field["message"] = std::string("Text analysis failed: ") + e.what();
                data_field["error_type"] = "cpp_exception";
                last_error_ = data_field["message"];
            }
            
            return data_field;
        }
    );
}

} // namespace EMPI
