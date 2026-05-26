#pragma once

#include "../core/UniversalAgent.hpp"
#include <string>
#include <memory>

namespace EMPI {

/**
 * @class InterfaceGenerator
 * @brief EMPI agent that generates interface HTML based on analysis results.
 * 
 * Uses φ-ψ handler architecture:
 * - φ-function: Extracts text metrics and feedback analysis
 * - ψ-function: Calls LLM to generate personalized HTML interface
 */
class InterfaceGenerator : public UniversalAgent {
public:
    /**
     * @brief Constructs an interface generator agent.
     * 
     * @param model_path Path to model file in models/ directory
     */
    explicit InterfaceGenerator(const std::string& model_path = "models/Phi-3-mini-4k-instruct-q4.gguf");
    
    ~InterfaceGenerator();
    
    bool is_available() const;
    std::string get_last_error() const;

private:
    void register_handlers();
    
    class LlamaImpl;
    std::unique_ptr<LlamaImpl> llama_impl_;
    std::string last_error_;
};

} // namespace EMPI
