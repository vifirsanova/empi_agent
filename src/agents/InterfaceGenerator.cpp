#include "InterfaceGenerator.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

InterfaceGenerator::InterfaceGenerator(std::shared_ptr<LLMClient> llm_client,
                                       const std::string& local_model_path)
    : UniversalAgent("interface_generator", "html_generation")
    , llm_client_(std::move(llm_client))
    , local_model_path_(local_model_path)
{
    register_handlers();
}

InterfaceGenerator::~InterfaceGenerator() = default;

bool InterfaceGenerator::is_available() const {
    return llm_client_ && llm_client_->is_available();
}

std::string InterfaceGenerator::strip_markdown_code_blocks(const std::string& html) {
    std::string result = html;
    
    const std::string open_block = "```html";
    const std::string close_block = "```";
    
    size_t start = result.find(open_block);
    if (start != std::string::npos) {
        result.erase(start, open_block.length());
    }
    
    start = result.find("```");
    if (start != std::string::npos && start < 50) {
        result.erase(start, 3);
    }
    
    size_t end = result.rfind(close_block);
    if (end != std::string::npos && end + 3 >= result.length() - 5) {
        result.erase(end, close_block.length());
    }
    
    while (!result.empty() && (result.front() == '\n' || result.front() == '\r' || result.front() == ' ')) {
        result.erase(0, 1);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    
    return result;
}

std::string InterfaceGenerator::fallback_html(const json& metrics, const json& feedback) const {
    std::stringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"UTF-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
       << "<title>Adapted Material</title>\n"
       << "<style>\n"
       << "  body { font-family: Arial, sans-serif; max-width: 800px; margin: 40px auto; "
       << "padding: 20px; line-height: 1.8; background: #fafafa; color: #222; }\n"
       << "  h1 { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 8px; }\n"
       << "  .card { background: #fff; padding: 20px; border-radius: 8px; "
       << "box-shadow: 0 1px 4px rgba(0,0,0,0.1); margin: 16px 0; }\n"
       << "  .note { background: #fff3cd; padding: 12px; border-left: 4px solid #ffc107; "
       << "border-radius: 4px; margin: 16px 0; }\n"
       << "</style>\n</head>\n<body>\n"
       << "<h1>Adapted Learning Material</h1>\n"
       << "<div class=\"note\">Fallback mode: no LLM backend available. "
       << "Configure API key or local model in agent_config.toml.</div>\n"
       << "<div class=\"card\"><h2>Text Metrics</h2><pre>"
       << metrics.dump(2) << "</pre></div>\n"
       << "<div class=\"card\"><h2>User Profile</h2><pre>"
       << feedback.dump(2) << "</pre></div>\n"
       << "</body>\n</html>";
    return ss.str();
}

void InterfaceGenerator::register_handlers() {
    register_handler("html_generation",
        [](const json& input, const json&, json& state) -> json {
            json ext;
            if (input.contains("text_metrics"))  ext["text_metrics"] = input["text_metrics"];
            if (input.contains("feedback_analysis")) ext["feedback_analysis"] = input["feedback_analysis"];
            if (input.contains("original_text")) ext["original_text"] = input["original_text"];
            if (!ext.contains("text_metrics")) {
                ext["error"] = "Missing text_metrics";
                return ext;
            }
            if (!ext.contains("feedback_analysis")) {
                ext["error"] = "Missing feedback_analysis";
                return ext;
            }
            state["total_generations"] = state.value("total_generations", 0) + 1;
            return ext;
        },
        [this](const json& ext, const json&, json& state) -> json {
            json out;
            if (ext.contains("error")) {
                out["status"] = "error";
                out["message"] = ext["error"];
                return out;
            }
            try {
                std::string html;
                
                // Try cloud API
                if (llm_client_ && llm_client_->is_available()) {
                    std::stringstream prompt;
                    prompt << "You are an accessibility assistant. Adapt this text for a user.\n\n";
                    prompt << "ORIGINAL TEXT:\n" << ext.value("original_text", "") << "\n\n";
                    prompt << "TEXT METRICS:\n" << ext["text_metrics"].dump(2) << "\n\n";
                    prompt << "USER PROFILE:\n" << ext["feedback_analysis"].dump(2) << "\n\n";
                    prompt << "Generate a complete HTML5 page with inline CSS adapted to their needs. ";
                    prompt << "Return ONLY the HTML starting with <!DOCTYPE html>. ";
                    prompt << "Do NOT wrap in markdown code blocks.";
                    
                    json resp = llm_client_->generate_json(prompt.str());
                    html = resp.value("text", "");
                }
                
                // Fallback if cloud API failed
                if (html.empty()) {
                    html = fallback_html(ext["text_metrics"], ext["feedback_analysis"]);
                }
                
                html = strip_markdown_code_blocks(html);
                
                out["status"] = "success";
                out["generation_id"] = "gen_" + std::to_string(state.value("total_generations", 0));
                out["html"] = html;
                out["html_size"] = html.length();
            } catch (const std::exception& e) {
                out["status"] = "error";
                out["message"] = std::string("Generation failed: ") + e.what();
                last_error_ = out["message"];
            }
            return out;
        }
    );
}

} // namespace EMPI
