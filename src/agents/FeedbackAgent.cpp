#include "FeedbackAgent.hpp"
#include <sstream>

using json = nlohmann::json;

namespace EMPI {

FeedbackAgent::FeedbackAgent(std::shared_ptr<LLMClient> llm_client)
    : UniversalAgent("feedback_agent", "feedback_analysis")
    , llm_client_(std::move(llm_client))
{
    register_handlers();
}

FeedbackAgent::~FeedbackAgent() = default;

bool FeedbackAgent::is_available() const {
    return llm_client_ && llm_client_->is_available();
}

std::string FeedbackAgent::get_last_error() const {
    return last_error_;
}

json FeedbackAgent::parse_response(const std::string& raw) const {
    size_t start = raw.find('{');
    size_t end = raw.rfind('}');
    if (start != std::string::npos && end != std::string::npos && start < end) {
        try {
            return json::parse(raw.substr(start, end - start + 1));
        } catch (...) {}
    }
    return fallback_analysis();
}

json FeedbackAgent::fallback_analysis() const {
    return {
        {"sentiment", "neutral"},
        {"topics", {"general"}},
        {"satisfaction_score", 0.5},
        {"complaints", json::array()},
        {"feedback_summary", "Analysis unavailable"}
    };
}

void FeedbackAgent::register_handlers() {
    register_handler("feedback_analysis",
        [](const json& input, const json&, json& state) -> json {
            json ext;
            if (input.contains("dialog_history") && input["dialog_history"].is_array()) {
                ext["dialog_history"] = input["dialog_history"];
            } else if (input.contains("history") && input["history"].is_array()) {
                ext["dialog_history"] = input["history"];
            } else if (input.contains("messages") && input["messages"].is_array()) {
                ext["dialog_history"] = input["messages"];
            } else {
                ext["error"] = "No dialog history found";
                return ext;
            }
            ext["message_count"] = ext["dialog_history"].size();
            state["total_analyses"] = state.value("total_analyses", 0) + 1;
            return ext;
        },
        [this](const json& ext, const json&, json& state) -> json {
            json out;
            if (ext.contains("error")) {
                out["status"] = "error";
                out["message"] = ext["error"];
                return out;
            }

            std::stringstream prompt;
            prompt << "Analyze this dialog history and return JSON with fields: ";
            prompt << "sentiment (positive/neutral/negative), topics (array), ";
            prompt << "satisfaction_score (0-1), complaints (array), feedback_summary.\n\n";
            prompt << "DIALOG:\n";
            for (const auto& msg : ext["dialog_history"]) {
                prompt << msg.value("role", "?") << ": " << msg.value("content", "") << "\n";
            }

            try {
                json analysis;
                if (llm_client_ && llm_client_->is_available()) {
                    json resp = llm_client_->generate_json(prompt.str());
                    std::string text = resp.value("text", "");
                    analysis = parse_response(text);
                } else {
                    analysis = fallback_analysis();
                }

                out["status"] = "success";
                out["analysis_id"] = "fb_" + std::to_string(state.value("total_analyses", 0));
                out["messages_analyzed"] = ext["message_count"];
                out["analysis"] = analysis;
            } catch (const std::exception& e) {
                out["status"] = "error";
                out["message"] = std::string("Analysis failed: ") + e.what();
                last_error_ = out["message"];
            }

            return out;
        }
    );
}

} // namespace EMPI
