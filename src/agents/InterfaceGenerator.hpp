#pragma once
#include "../core/UniversalAgent.hpp"
#include "../core/LLMClient.hpp"
#include <string>
#include <memory>
#include "llama.h"

namespace EMPI {

class InterfaceGenerator : public UniversalAgent {
public:
    InterfaceGenerator(std::shared_ptr<LLMClient> llm_client,
                       const std::string& local_model_path = "");
    ~InterfaceGenerator() override;

    bool is_available() const;
    std::string get_last_error() const;

private:
    void register_handlers();
    std::string fallback_html(const nlohmann::json& metrics,
                              const nlohmann::json& feedback) const;

    std::shared_ptr<LLMClient> llm_client_;
    std::string local_model_path_;
    std::string last_error_;

    class LlamaImpl;
    std::unique_ptr<LlamaImpl> llama_impl_;
};

} // namespace EMPI
