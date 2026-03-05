/**
 * @file FeedbackAgent.cpp
 * @brief Implementation of FeedbackAgent for user profile analysis
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

#include "llama.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace EMPI {

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
        
        ss << "[INST] Analyze this dialog and create a user profile. Focus on:\n";
        ss << "- Age group (child, teen, adult, senior)\n";
        ss << "- ADHD indicators (inattention, hyperactivity, impulsivity in responses)\n";
        ss << "- Dyslexia indicators (spelling patterns, sentence structure)\n";
        ss << "- Special needs (anxiety, autism spectrum indicators, processing needs)\n";
        ss << "- Communication style (direct/indirect, emotional/cognitive)\n";
        ss << "- Technical literacy (basic/intermediate/advanced)\n\n";
        
        ss << "DIALOG:\n";
        for (const auto& msg : dialog_history) {
            std::string role = msg.value("role", "unknown");
            std::string content = msg.value("content", "");
            ss << role << ": " << content << "\n";
        }
        
        ss << "\nReturn ONLY a JSON object with this structure:\n";
        ss << "{\n";
        ss << "  \"age_group\": \"child/teen/adult/senior\",\n";
        ss << "  \"adhd_indicators\": {\n";
        ss << "    \"inattention\": 0.0-1.0,\n";
        ss << "    \"hyperactivity\": 0.0-1.0,\n";
        ss << "    \"impulsivity\": 0.0-1.0\n";
        ss << "  },\n";
        ss << "  \"dyslexia_indicators\": {\n";
        ss << "    \"spelling_issues\": 0.0-1.0,\n";
        ss << "    \"sentence_structure\": 0.0-1.0,\n";
        ss << "    \"reading_difficulty\": 0.0-1.0\n";
        ss << "  },\n";
        ss << "  \"special_needs\": {\n";
        ss << "    \"anxiety_indicators\": 0.0-1.0,\n";
        ss << "    \"autism_indicators\": 0.0-1.0,\n";
        ss << "    \"processing_speed\": \"slow/medium/fast\"\n";
        ss << "  },\n";
        ss << "  \"communication_style\": {\n";
        ss << "    \"directness\": 0.0-1.0,\n";
        ss << "    \"emotionality\": 0.0-1.0,\n";
        ss << "    \"verbosity\": 0.0-1.0\n";
        ss << "  },\n";
        ss << "  \"technical_literacy\": \"basic/intermediate/advanced\",\n";
        ss << "  \"summary\": \"brief profile summary\"\n";
        ss << "}\n";
        ss << "[/INST]";
        
        return ss.str();
    }
    
    std::string generate_text(const std::string& prompt, int max_tokens) {
        std::string result;
        std::vector<llama_token> tokens;
        
        int n_tokens = llama_tokenize(vocab_, prompt.c_str(), prompt.length(), nullptr, 0, true, true);
        tokens.resize(n_tokens);
        llama_tokenize(vocab_, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);
        
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
            
            if (result.find("}") != std::string::npos) {
                size_t last_brace = result.rfind('}');
                size_t first_brace = result.find('{');
                if (last_brace > first_brace && 
                    result.substr(first_brace, last_brace - first_brace + 1).find('{') == 0) {
                    break;
                }
            }
            
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
                // Return empty but structured JSON
            }
        }
        
        // Return default structure if parsing fails
        return {
            {"age_group", "adult"},
            {"adhd_indicators", {
                {"inattention", 0.5},
                {"hyperactivity", 0.5},
                {"impulsivity", 0.5}
            }},
            {"dyslexia_indicators", {
                {"spelling_issues", 0.5},
                {"sentence_structure", 0.5},
                {"reading_difficulty", 0.5}
            }},
            {"special_needs", {
                {"anxiety_indicators", 0.5},
                {"autism_indicators", 0.5},
                {"processing_speed", "medium"}
            }},
            {"communication_style", {
                {"directness", 0.5},
                {"emotionality", 0.5},
                {"verbosity", 0.5}
            }},
            {"technical_literacy", "intermediate"},
            {"summary", "Profile analysis based on dialog"}
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
            
            // Просто берем то что пришло - без валидации
            if (input.contains("dialog_history")) {
                extracted_info["dialog_history"] = input["dialog_history"];
            } else if (input.contains("history")) {
                extracted_info["dialog_history"] = input["history"];
            } else if (input.contains("messages")) {
                extracted_info["dialog_history"] = input["messages"];
            } else {
                // Если ничего не нашли, берем весь input как диалог
                extracted_info["dialog_history"] = input;
            }
            
            state["total_analyses"] = state.value("total_analyses", 0) + 1;
            
            return extracted_info;
        },
        
        [this](const json& extracted_info, const json& context, json& state) -> json {
            json data_field;
            
            try {
                json profile;
                if (llama_impl_ && is_available()) {
                    profile = llama_impl_->analyze_feedback(extracted_info["dialog_history"]);
                } else {
                    throw std::runtime_error("LLM not available");
                }
                
                data_field["status"] = "success";
                data_field["profile_id"] = "profile_" + std::to_string(state.value("total_analyses", 0));
                data_field["user_profile"] = profile;
                
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
