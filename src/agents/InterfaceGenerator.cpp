#include "InterfaceGenerator.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <sstream>
#include <thread>
#ifndef EMPI_NO_LLAMA
#include "llama.h"
#endif
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

#ifndef EMPI_NO_LLAMA
class InterfaceGenerator::LlamaImpl {
public:
    LlamaImpl(const std::string& model_path)
        : model_(nullptr), ctx_(nullptr), sampler_(nullptr), vocab_(nullptr), is_available_(false)
    {
        if (!model_path.empty() && fs::exists(model_path)) {
            try { load_model(model_path); }
            catch (const std::exception& e) { last_error_ = e.what(); }
        }
    }

    ~LlamaImpl() {
        if (sampler_) llama_sampler_free(sampler_);
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    bool is_available() const { return is_available_; }
    std::string get_last_error() const { return last_error_; }

    std::string generate_interface(const json& text_metrics,
                                   const json& feedback_analysis,
                                   const std::string& original_text) {
        if (!is_available_) throw std::runtime_error("Local model not available");
        return generate_html(construct_prompt(text_metrics, feedback_analysis, original_text));
    }

private:
    llama_model* model_;
    llama_context* ctx_;
    llama_sampler* sampler_;
    const llama_vocab* vocab_;
    bool is_available_;
    std::string last_error_;

    void load_model(const std::string& model_path) {
        llama_backend_init();
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 99;
        model_ = llama_model_load_from_file(model_path.c_str(), model_params);
        if (!model_) throw std::runtime_error("Failed to load model");

        vocab_ = llama_model_get_vocab(model_);
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 4096;
        ctx_params.n_batch = 2048;
        ctx_params.n_threads = std::thread::hardware_concurrency();
        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) { llama_model_free(model_); throw std::runtime_error("Failed to create context"); }

        llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
        sampler_ = llama_sampler_chain_init(sampler_params);
        llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        is_available_ = true;
    }

    std::string construct_prompt(const json& metrics, const json& profile, const std::string& text) {
        std::stringstream ss;
        ss << "[INST] You are an accessibility assistant. Adapt the following text for a user with specific needs.\n\n";
        ss << "ORIGINAL TEXT:\n" << text << "\n\n";
        ss << "TEXT METRICS:\n" << metrics.dump(2) << "\n\n";
        ss << "USER PROFILE:\n" << profile.dump(2) << "\n\n";
        ss << "TASK:\n";
        ss << "1. Analyze the user profile (ADHD, dyslexia, low vision, etc.)\n";
        ss << "2. Adapt the text: simplify, restructure, format\n";
        ss << "3. Generate a complete HTML5 page with inline CSS\n";
        ss << "4. Use appropriate fonts, spacing, contrast for their needs\n";
        ss << "5. Return ONLY the HTML starting with <!DOCTYPE html>\n\n";
        ss << "ADAPTED HTML:\n[/INST]\n";
        return ss.str();
    }

    std::string generate_html(const std::string& prompt) {
        std::string result;
        std::vector<llama_token> tokens;

        int n = llama_tokenize(vocab_, prompt.c_str(), prompt.length(), nullptr, 0, true, true);
        tokens.resize(std::abs(n));
        llama_tokenize(vocab_, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);

        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        if (llama_decode(ctx_, batch) != 0) throw std::runtime_error("Decode failed");

        for (int i = 0; i < 500; i++) {
            llama_token new_token = llama_sampler_sample(sampler_, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, new_token)) break;

            char buf[256];
            int len = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf), 0, true);
            if (len <= 0) break;

            std::string piece(buf, len);
            result += piece;
            if (result.find("</html>") != std::string::npos) break;

            batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx_, batch) != 0) break;
        }
        return result;
    }
};
#endif

InterfaceGenerator::InterfaceGenerator(std::shared_ptr<LLMClient> llm_client,
                                       const std::string& local_model_path)
    : UniversalAgent("interface_generator", "html_generation")
    , llm_client_(std::move(llm_client))
    , local_model_path_(local_model_path)
#ifndef EMPI_NO_LLAMA
    , llama_impl_(nullptr)
#endif
    , llama_initialized_(false)
{
    register_handlers();
}

InterfaceGenerator::~InterfaceGenerator() = default;

bool InterfaceGenerator::is_available() const {
    bool available = (llm_client_ && llm_client_->is_available());
#ifndef EMPI_NO_LLAMA
    available = available || (llama_impl_ && llama_impl_->is_available());
#endif
    return available;
}

void InterfaceGenerator::conditional_llama_init() {
#ifndef EMPI_NO_LLAMA
    if (llama_initialized_ || local_model_path_.empty()) return;
    
    if (llm_client_ && llm_client_->is_available()) return;
    
    try {
        llama_impl_ = std::make_unique<LlamaImpl>(local_model_path_);
        llama_initialized_ = true;
        if (llama_impl_->is_available()) {
            std::cout << "[InterfaceGenerator] Lazy-loaded llama.cpp model" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[InterfaceGenerator] Failed to load llama: " << e.what() << std::endl;
        llama_impl_ = nullptr;
    }
#endif
}

std::string InterfaceGenerator::strip_markdown_code_blocks(const std::string& html) {
    std::string result = html;
    
    // Remove ```html at beginning
    const std::string open_block = "```html";
    const std::string close_block = "```";
    
    size_t start = result.find(open_block);
    if (start != std::string::npos) {
        result.erase(start, open_block.length());
    }
    
    // Also check for just ```
    start = result.find("```");
    if (start != std::string::npos && start < 50) {  // Only near beginning
        result.erase(start, 3);
    }
    
    // Remove closing ```
    size_t end = result.rfind(close_block);
    if (end != std::string::npos && end + 3 >= result.length() - 5) {
        result.erase(end, close_block.length());
    }
    
    // Trim whitespace
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
        // φ-function: Extract data from input
        [](const json& input, const json&, json& state) -> json {
            json ext;
            if (input.contains("text_metrics"))  ext["text_metrics"] = input["text_metrics"];
            if (input.contains("feedback_analysis")) ext["feedback_analysis"] = input["feedback_analysis"];
            if (input.contains("original_text")) ext["original_text"] = input["original_text"];
            if (!ext.contains("text_metrics")) { ext["error"] = "Missing text_metrics"; return ext; }
            if (!ext.contains("feedback_analysis")) { ext["error"] = "Missing feedback_analysis"; return ext; }
            state["total_generations"] = state.value("total_generations", 0) + 1;
            return ext;
        },
        // ψ-function: Process with LLM/llama
        [this](const json& ext, const json&, json& state) -> json {
            json out;
            if (ext.contains("error")) {
                out["status"] = "error";
                out["message"] = ext["error"];
                return out;
            }
            try {
                std::string html;
                
                // First try cloud API
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
                
                // Fall back to llama if API failed
                if (html.empty()) {
		#ifndef EMPI_NO_LLAMA
                    conditional_llama_init();
                    if (llama_impl_ && llama_impl_->is_available()) {
                        html = llama_impl_->generate_interface(
                            ext["text_metrics"],
                            ext["feedback_analysis"],
                            ext.value("original_text", "")
                        );
                    } else {
                        html = fallback_html(ext["text_metrics"], ext["feedback_analysis"]);
                    }
		#endif
                }
                
                // Post-Processing
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
