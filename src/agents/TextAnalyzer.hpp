#pragma once

#include "../core/UniversalAgent.hpp"
#include <string>
#include <memory>

namespace EMPI {

/**
 * @class TextAnalyzer
 * @brief EMPI agent for text analysis via Python NLP.
 * 
 * Uses φ-ψ handler architecture:
 * - φ-function: Extracts input data, updates state
 * - ψ-function: Calls Python script, formats EMPI response
 * 
 * Input format: {"text": "text to analyze", "language": "en/ru/..."}
 * Output format: EMPI message with analysis data in 'data' field
 */
class TextAnalyzer : public UniversalAgent {
public:
    /**
     * @brief Constructs a text analysis agent.
     * 
     * @param python_path Preferred path to Python interpreter.
     *                    If empty, automatically searches for Python 3.8+.
     * @throws std::runtime_error If Python or script is not found.
     */
    explicit TextAnalyzer(const std::string& python_path = "");
    
    ~TextAnalyzer();
    
    /**
     * @brief Checks agent availability (Python + script).
     * 
     * @return true If Python interpreter and text_analyzer.py script are available.
     * @return false Otherwise.
     */
    bool is_available() const;
    
    /**
     * @brief Gets the last error message.
     * 
     * @return std::string Error message or empty string.
     */
    std::string get_last_error() const;
    
    /**
     * @brief Gets the path to the Python interpreter being used.
     * 
     * @return std::string Path to Python executable.
     */
    std::string get_python_path() const;
    
    /**
     * @brief Gets the absolute path to the script being used.
     * 
     * @return std::string Path to text_analyzer.py.
     */
    std::string get_script_path() const;

private:
    /**
     * @brief Registers EMPI protocol handlers (φ and ψ functions).
     */
    void register_handlers();
    
    /**
     * @private
     * @brief Internal class for Python subprocess management.
     */
    class PythonSubprocessImpl;
    
    /**
     * @private
     * @brief Implementation of Python subprocess operations.
     */
    std::unique_ptr<PythonSubprocessImpl> python_impl_;
    
    /**
     * @private
     * @brief Stores the last error message.
     */
    std::string last_error_;
};

} // namespace EMPI
