#include "UniversalAgent.hpp"
#include <stdexcept>

namespace EMPI {

UniversalAgent::UniversalAgent(const std::string& agent_id, 
                               const std::string& default_task_type)
    : agent_id_(agent_id)
    , default_task_type_(default_task_type.empty() ? agent_id : default_task_type)
    , state_(json::object())
{
    // Initialize with empty state
}

json UniversalAgent::process_raw(const json& input, const std::string& task_type) {
    std::string task = task_type.empty() ? default_task_type_ : task_type;
    
    // 1. Create EMPI header
    json empi_message = create_empi_message(task);
    
    // Check if handler exists for this task type
    auto handler_it = handlers_.find(task);
    if (handler_it == handlers_.end()) {
        json error_result;
        error_result["status"] = "error";
        error_result["message"] = "No handler registered for task type: " + task;
        error_result["error_type"] = "handler_not_found";
        
        empi_message["payload"]["data"] = error_result;
        return empi_message;
    }
    
    // 2. Execute φ-ψ functions
    auto& phi_function = handler_it->second.phi_function;
    auto& psi_function = handler_it->second.psi_function;

    try {
        // Execute φ-function (data extraction)
        json extracted = phi_function(input, json{}, state_);
        
        // Execute ψ-function (data processing)
        json data_result = psi_function(extracted, json{}, state_);
        
        // 3. Place result in data field
        empi_message["payload"]["data"] = data_result;
        
    } catch (const std::exception& e) {
        // Handle exceptions during φ-ψ execution
        json error_result;
        error_result["status"] = "error";
        error_result["message"] = std::string("Processing failed: ") + e.what();
        error_result["error_type"] = "processing_exception";
        
        empi_message["payload"]["data"] = error_result;
    }
    
    return empi_message;
}

void UniversalAgent::register_handler(
    const std::string& task_type,
    std::function<json(const json&, const json&, json&)> phi_function,
    std::function<json(const json&, const json&, json&)> psi_function
) {
    if (task_type.empty()) {
        throw std::invalid_argument("Task type cannot be empty");
    }
    
    if (!phi_function || !psi_function) {
        throw std::invalid_argument("Both φ and ψ functions must be callable");
    }
    
    handlers_[task_type] = HandlerPair{std::move(phi_function), std::move(psi_function)};
}

json UniversalAgent::create_empi_message(const std::string& task_type) const {
    json empi_message;
    
    // EMPI Protocol Header
    empi_message["header"] = {
        {"protocol", "EMPI/1.0"},
        {"message_id", "msg_" + std::to_string(std::time(nullptr)) + "_" + agent_id_},
        {"timestamp", std::to_string(std::time(nullptr))},
        {"agent_id", agent_id_},
        {"task_type", task_type},
        {"version", "1.0"}
    };
    
    // EMPI Payload Structure
    empi_message["payload"] = {
        {"metadata", {
            {"source", agent_id_},
            {"processing_start", std::to_string(std::time(nullptr))}
        }},
        {"data", json::object()}
    };
    
    return empi_message;
}

} // namespace EMPI
