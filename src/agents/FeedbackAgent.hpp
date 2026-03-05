#pragma once

#include "../core/UniversalAgent.hpp"
#include <string>
#include <memory>

namespace EMPI {

/**
 * @class FeedbackAgent
 * @brief EMPI agent for user feedback analysis based on dialog history.
 * 
 * Uses φ-ψ handler architecture:
 * - φ-function: Extracts dialog history from input, updates state
 * - ψ-function: Calls Python script or LLM to analyze user feedback
 */
class FeedbackAgent : public UniversalAgent {
public:
    /**
     * @brief Constructs a feedback analysis agent.
     * 
     * @param model_path Path to model file in models/ directory
     * @throws std::runtime_error If model is not found
     */
    explicit FeedbackAgent(const std::string& model_path = "models/Phi-3-mini-4k-instruct-q4.gguf");
    
    ~FeedbackAgent();
    
    /**
     * @brief Checks agent availability.
     */
    bool is_available() const;
    
    /**
     * @brief Gets the last error message.
     */
    std::string get_last_error() const;

private:
    /**
     * @brief Registers EMPI protocol handlers.
     */
    void register_handlers();
    
    /**
     * @private
     * @brief Internal class for LLM integration
     */
    class LlamaImpl;
    std::unique_ptr<LlamaImpl> llama_impl_;
    std::string last_error_;
};

} // namespace EMPI
