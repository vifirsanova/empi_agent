/**
 * @file InterfaceGenerator.cpp
 * @brief Implementation of InterfaceGenerator for HTML generation
 */

#include "InterfaceGenerator.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "llama.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

class InterfaceGenerator::LlamaImpl {
public:
    LlamaImpl(const std::string& model_path)
        : model_(nullptr)
        , ctx_(nullptr)
        , sampler_(nullptr)
        , vocab_(nullptr)
        , is_available_(false)
    {
        if (fs::exists(model_path)) {
            try {
                load_model(model_path);
            } catch (const std::exception& e) {
                last_error_ = e.what();
            }
        } else {
            last_error_ = "Model not found: " + model_path;
        }
    }
    
    ~LlamaImpl() {
        if (sampler_) llama_sampler_free(sampler_);
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }
    
    bool is_available() const { return is_available_; }
    std::string get_last_error() const { return last_error_; }
    
    std::string generate_interface(const json& text_metrics, const json& feedback_analysis, const std::string&original_text) {
        if (!is_available_) {
            throw std::runtime_error("Model not available: " + last_error_);
        }
        
        std::string prompt = construct_prompt(text_metrics, feedback_analysis, original_text);
        return generate_html(prompt);
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
        if (!model_) {
            throw std::runtime_error("Failed to load model: " + model_path);
        }
        
        vocab_ = llama_model_get_vocab(model_);
        
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 4096;
        ctx_params.n_batch = 2048;
        ctx_params.n_threads = std::thread::hardware_concurrency();
        
        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) {
            llama_model_free(model_);
            throw std::runtime_error("Failed to create context");
        }
        
        llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
        sampler_ = llama_sampler_chain_init(sampler_params);
        llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.8f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        
        is_available_ = true;
    }
   
    std::string construct_prompt(const json& text_metrics, const json& user_profile, const std::string& original_text) { 
    std::stringstream ss;
    
    ss << "[INST] You are an accessibility assistant. Adapt the following text for a user with specific needs.\n\n";
    
    ss << "ORIGINAL TEXT:\n";
    ss << original_text << "\n\n";

    ss << "ORIGINAL TEXT METRICS:\n";
    ss << text_metrics.dump(2) << "\n\n";
    
    ss << "USER PROFILE:\n";
    ss << user_profile.dump(2) << "\n\n";
    
    ss << "TASK:\n";
    ss << "1. Analyze the user's profile (age, ADHD, dyslexia, special needs)\n";
    ss << "2. Rewrite/adapt the original text to match their needs\n";
    ss << "3. Generate a complete HTML page with the ADAPTED text\n";
    ss << "4. Use appropriate formatting based on their needs:\n";
    ss << "   - For dyslexia: Use OpenDyslexic font, larger spacing, cream background\n";
    ss << "   - For ADHD: Short paragraphs, clear headings, highlighted key points\n";
    ss << "   - For low vision: High contrast, large text\n";
    ss << "   - For autism: Clear structure, literal language, avoid idioms\n";
    ss << "   - For children: Simpler words, colorful, engaging\n";
    ss << "   - For seniors: Larger text, simple navigation\n\n";
    
    ss << "OUTPUT FORMAT:\n";
    ss << "- Complete HTML5 page with inline CSS\n";
    ss << "- Show both original metrics and adapted version\n";
    ss << "- Explain what adaptations were made for this user\n";
    ss << "- Make it practical and usable\n\n";
    
    ss << "ADAPTED HTML:\n";
    ss << "[/INST]\n";
    
    return ss.str();
}
    
    std::string generate_html(const std::string& prompt) {
        std::string result;
        std::vector<llama_token> tokens;
        
        // Токенизация с add_bos=true как в command-inference.cpp
        int n_tokens = llama_tokenize(vocab_, prompt.c_str(), prompt.length(), nullptr, 0, true, true);
        if (n_tokens < 0) {
            tokens.resize(-n_tokens);
            llama_tokenize(vocab_, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);
        } else {
            tokens.resize(n_tokens);
            llama_tokenize(vocab_, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);
        }
        
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        if (llama_decode(ctx_, batch) != 0) {
            throw std::runtime_error("Failed to decode prompt");
        }
        
        // Генерация как в command-inference.cpp
        for (int i = 0; i < 500; i++) {
            llama_token new_token = llama_sampler_sample(sampler_, ctx_, -1);
            
            if (llama_vocab_is_eog(vocab_, new_token)) {
                break;
            }
            
            char buf[256];
            int n = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf), 0, true);
            if (n <= 0) {
                break;
            }
            
            std::string piece(buf, n);
            
            // Останавливаемся на маркере конца
            if (piece.find("</html>") != std::string::npos) {
                result += piece;
                break;
            }
            
            result += piece;
            
            batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx_, batch) != 0) {
                break;
            }
        }
        
        return result;
    }
};

InterfaceGenerator::InterfaceGenerator(const std::string& model_path)
    : UniversalAgent("interface_generator", "html_generation")
{
    try {
        llama_impl_ = std::make_unique<LlamaImpl>(model_path);
    } catch (const std::exception& e) {
        last_error_ = e.what();
        llama_impl_ = nullptr;
    }
    
    register_handlers();
}

InterfaceGenerator::~InterfaceGenerator() = default;

bool InterfaceGenerator::is_available() const {
    return llama_impl_ && llama_impl_->is_available();
}

std::string InterfaceGenerator::get_last_error() const {
    return last_error_;
}

void InterfaceGenerator::register_handlers() {
    register_handler("html_generation",
        // φ-function
        [](const json& input, const json& context, json& state) -> json {
            json extracted_info;
            
            if (input.contains("text_metrics")) {
                extracted_info["text_metrics"] = input["text_metrics"];
            }
            if (input.contains("feedback_analysis")) {
                extracted_info["feedback_analysis"] = input["feedback_analysis"];
            }
            if (input.contains("original_text")) {
                extracted_info["original_text"] = input["original_text"];
            } 
 
            if (!extracted_info.contains("text_metrics")) {
                extracted_info["error"] = "Missing text_metrics";
                return extracted_info;
            }
            if (!extracted_info.contains("feedback_analysis")) {
                extracted_info["error"] = "Missing feedback_analysis";
                return extracted_info;
            }
            state["total_generations"] = state.value("total_generations", 0) + 1;
            
            return extracted_info;
        },
        
        // ψ-function
        [this](const json& extracted_info, const json& context, json& state) -> json {
            json data_field;
            
            if (extracted_info.contains("error")) {
                data_field["status"] = "error";
                data_field["message"] = extracted_info["error"];
                return data_field;
            }
            
            try {
                std::string html;
                if (llama_impl_ && is_available()) {
                    html = llama_impl_->generate_interface(
                        extracted_info["text_metrics"],
                        extracted_info["feedback_analysis"],
			extracted_info.value("original_text", "")
                    );
                } else {
                    // Fallback HTML template
                    html = R"(<!DOCTYPE html>
<html>
<head><title>Analysis Results</title>
<style>body{font-family:Arial;margin:0;padding:20px;background:#f5f5f5}.container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}h1{color:#2c3e50}.section{margin:20px 0;padding:15px;background:#ecf0f1;border-radius:3px}</style>
</head>
<body><div class="container"><h1>Analysis Results</h1><div class="section"><h3>Text Metrics</h3><pre>)" + extracted_info["text_metrics"].dump(2) + R"(</pre></div><div class="section"><h3>Feedback Analysis</h3><pre>)" + extracted_info["feedback_analysis"].dump(2) + R"(</pre></div></div></body>
</html>)";
                }
                
                data_field["status"] = "success";
                data_field["generation_id"] = "gen_" + std::to_string(state.value("total_generations", 0));
                data_field["html"] = html;
                data_field["html_size"] = html.length();
                
            } catch (const std::exception& e) {
                data_field["status"] = "error";
                data_field["message"] = std::string("Generation failed: ") + e.what();
                last_error_ = data_field["message"];
            }
            
            return data_field;
        }
    );
}

} // namespace EMPI
