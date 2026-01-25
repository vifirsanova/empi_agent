#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace EMPI {

/**
 * @class UniversalAgent
 * @brief Base class for all EMPI agents implementing φ-ψ handler architecture.
 * 
 * Provides:
 * - EMPI message formatting
 * - State management
 * - φ-ψ function registration and execution
 */
class UniversalAgent {
public:
    /**
     * @brief Constructs a UniversalAgent with specified identity.
     * 
     * @param agent_id Unique identifier for the agent
     * @param default_task_type Default task type for processing
     */
    explicit UniversalAgent(const std::string& agent_id, 
                           const std::string& default_task_type = "");
    
    virtual ~UniversalAgent() = default;
    
    /**
     * @brief Process input data using EMPI protocol with φ-ψ functions.
     * 
     * @param input Raw input data in JSON format
     * @param task_type Specific task type to execute (uses default if empty)
     * @return json Complete EMPI message with processed data
     */
    json process_raw(const json& input, const std::string& task_type = "");
    
    /**
     * @brief Get the agent's unique identifier.
     * 
     * @return std::string Agent ID
     */
    std::string get_agent_id() const { return agent_id_; }
    
    /**
     * @brief Get the agent's default task type.
     * 
     * @return std::string Default task type
     */
    std::string get_default_task_type() const { return default_task_type_; }
    
    /**
     * @brief Get the agent's current state.
     * 
     * @return json Current agent state
     */
    json get_agent_state() const { return state_; }
    
    /**
     * @brief Set the agent's state.
     * 
     * @param state New state to set
     */
    void set_agent_state(const json& state) { state_ = state; }
    
    /**
     * @brief Reset agent state to empty.
     */
    void reset_state() { state_ = json::object(); }
    
protected:
    /**
     * @brief Register φ-ψ function pair for a specific task type.
     * 
     * @param task_type Task type identifier
     * @param phi_function φ-function for data extraction
     * @param psi_function ψ-function for data processing
     */
    void register_handler(
        const std::string& task_type,
        std::function<json(const json&, const json&, json&)> phi_function,
        std::function<json(const json&, const json&, json&)> psi_function
    );
    
    /**
     * @brief Create a standard EMPI message header.
     * 
     * @param task_type Task type for the message
     * @return json EMPI message with header
     */
    json create_empi_message(const std::string& task_type) const;

private:
    struct HandlerPair {
        std::function<json(const json&, const json&, json&)> phi_function;
        std::function<json(const json&, const json&, json&)> psi_function;
    };
    
    std::string agent_id_;
    std::string default_task_type_;
    json state_;
    std::unordered_map<std::string, HandlerPair> handlers_;
};

} // namespace EMPI
