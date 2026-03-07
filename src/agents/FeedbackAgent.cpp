/**
 * @file FeedbackAgent.cpp
 * @brief Implementation of FeedbackAgent for user feedback analysis
 */

#include "FeedbackAgent.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

// Llama.cpp includes
#include <llama.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

/**
 * @class FeedbackAgent::LlamaImpl
 * @brief Manages llama.cpp model for feedback analysis
 */
class FeedbackAgent::LlamaImpl {
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
    
    json analyze_feedback(const json& dialog_history) {
        if (!is_available_) {
            throw std::runtime_error("Model not available: " + last_error_);
        }
        
        std::string prompt = construct_prompt(dialog_history);
        std::string response = generate_text(prompt, 512);
        
        return parse_response(response);
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
        ctx_params.n_ctx = 2048;
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
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        
        is_available_ = true;
    }
    
    std::string construct_prompt(const json& dialog_history) {
        std::stringstream ss;
        
        ss << "[INST] Analyze this dialog history and provide user feedback analysis. ";
        ss << "Extract: sentiment, key topics, user satisfaction, and any complaints.\n\n";
        
        ss << "DIALOG HISTORY:\n";
        for (const auto& msg : dialog_history) {
            std::string role = msg.value("role", "unknown");
            std::string content = msg.value("content", "");
            ss << role << ": " << content << "\n";
        }
        
        ss << "\nProvide analysis in JSON format with fields: ";
        ss << "sentiment (positive/neutral/negative), topics (array), ";
        ss << "satisfaction_score (0-1), complaints (array), feedback_summary\n";
        ss << "[/INST]\n";
        
        return ss.str();
    }
    
    std::string generate_text(const std::string& prompt, int max_tokens) {
        std::string result;
        std::vector<llama_token> tokens;
        
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
        
        for (int i = 0; i < max_tokens; i++) {
            llama_token new_token = llama_sampler_sample(sampler_, ctx_, -1);
            
            if (llama_vocab_is_eog(vocab_, new_token)) {
                break;
            }
            
            char buf[256];
            int n = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf), 0, true);
            if (n < 0) break;
            
            result += std::string(buf, n);
            
            batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx_, batch) != 0) break;
        }
        
        return result;
    }
    
    json parse_response(const std::string& response) {
        size_t json_start = response.find('{');
        size_t json_end = response.rfind('}');
        
        if (json_start != std::string::npos && json_end != std::string::npos) {
            try {
                return json::parse(response.substr(json_start, json_end - json_start + 1));
            } catch (...) {
            }
        }
        
        return {
            {"sentiment", "neutral"},
            {"topics", json::array()},
            {"satisfaction_score", 0.5},
            {"complaints", json::array()},
            {"feedback_summary", response.substr(0, 200)}
        };
    }
};

FeedbackAgent::FeedbackAgent(const std::string& model_path)
    : UniversalAgent("feedback_agent", "feedback_analysis")
{
    try {
        llama_impl_ = std::make_unique<LlamaImpl>(model_path);
    } catch (const std::exception& e) {
        last_error_ = e.what();
        llama_impl_ = nullptr;
    }
    
    register_handlers();
}

FeedbackAgent::~FeedbackAgent() = default;

bool FeedbackAgent::is_available() const {
    return llama_impl_ && llama_impl_->is_available();
}

std::string FeedbackAgent::get_last_error() const {
    return last_error_;
}

void FeedbackAgent::register_handlers() {
    register_handler("feedback_analysis",
        [](const json& input, const json& context, json& state) -> json {
            json extracted_info;
            
            std::vector<json> dialog;
            if (input.contains("dialog_history") && input["dialog_history"].is_array()) {
                dialog = input["dialog_history"].get<std::vector<json>>();
            } else if (input.contains("history") && input["history"].is_array()) {
                dialog = input["history"].get<std::vector<json>>();
            } else if (input.contains("messages") && input["messages"].is_array()) {
                dialog = input["messages"].get<std::vector<json>>();
            }
            
            if (dialog.empty()) {
                extracted_info["error"] = "No dialog history found";
                return extracted_info;
            }
            
            extracted_info["dialog_history"] = dialog;
            extracted_info["message_count"] = dialog.size();
            
            state["total_analyses"] = state.value("total_analyses", 0) + 1;
            
            return extracted_info;
        },
        
        [this](const json& extracted_info, const json& context, json& state) -> json {
            json data_field;
            
            if (extracted_info.contains("error")) {
                data_field["status"] = "error";
                data_field["message"] = extracted_info["error"];
                return data_field;
            }
            
            try {
                json analysis;
                if (llama_impl_ && is_available()) {
                    analysis = llama_impl_->analyze_feedback(extracted_info["dialog_history"]);
                } else {
                    analysis = {
                        {"sentiment", "neutral"},
                        {"topics", {"general"}},
                        {"satisfaction_score", 0.5},
                        {"complaints", json::array()},
                        {"feedback_summary", "Mock feedback analysis"}
                    };
                }
                
                data_field["status"] = "success";
                data_field["analysis_id"] = "fb_" + std::to_string(state.value("total_analyses", 0));
                data_field["analysis"] = analysis;
                data_field["messages_analyzed"] = extracted_info["message_count"];
                
            } catch (const std::exception& e) {
                data_field["status"] = "error";
                data_field["message"] = std::string("Analysis failed: ") + e.what();
                last_error_ = data_field["message"];
            }
            
            return data_field;
        }
    );
}

} // namespace EMPI
