#pragma once
#include "../core/UniversalAgent.hpp"
#include "../core/LLMClient.hpp"
#include <memory>
#include <string>

namespace EMPI {

class FeedbackAgent : public UniversalAgent {
public:
    explicit FeedbackAgent(std::shared_ptr<LLMClient> llm_client);
    ~FeedbackAgent() override;

    bool is_available() const;
    std::string get_last_error() const;

private:
    void register_handlers();
    json parse_response(const std::string& raw) const;
    json fallback_analysis() const;

    std::shared_ptr<LLMClient> llm_client_;
    std::string last_error_;
};

} // namespace EMPI
