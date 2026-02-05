#include "llama.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class EMPIDialogRecorder {
private:
    std::string session_id_;
    std::string output_file_;
    std::vector<json> history_;

public:
    EMPIDialogRecorder(const std::string& session_id = "", 
                       const std::string& output_file = "dialog_history.json")
        : session_id_(session_id.empty() ? generate_session_id() : session_id)
        , output_file_(output_file)
    {
        // Инициализируем пустую историю
        history_ = json::array();
    }
    
    void record_user_message(const std::string& message) {
        json msg = create_empi_message("user_input", {
            {"text", message},
            {"role", "user"},
            {"timestamp_ms", current_timestamp_ms()}
        });
        
        history_.push_back(msg);
        save_to_file();
    }
    
    void record_assistant_message(const std::string& message) {
        json msg = create_empi_message("assistant_response", {
            {"text", message},
            {"role", "assistant"},
            {"timestamp_ms", current_timestamp_ms()}
        });
        
        history_.push_back(msg);
        save_to_file();
    }
    
    json get_full_history_empi() const {
        return create_empi_message("dialog_history", {
            {"session_id", session_id_},
            {"message_count", history_.size()},
            {"messages", history_}
        });
    }
    
    json get_simple_history() const {
        json simple = json::array();
        for (const auto& msg : history_) {
            simple.push_back({
                {"role", msg["payload"]["data"]["role"]},
                {"content", msg["payload"]["data"]["text"]},
                {"timestamp", msg["payload"]["data"]["timestamp_ms"]}
            });
        }
        return simple;
    }
    
    void save_to_file(const std::string& filename = "") {
        std::string file = filename.empty() ? output_file_ : filename;
        std::ofstream out(file);
        out << get_full_history_empi().dump(2);
    }
    
    void clear_history() {
        history_.clear();
    }
    
    std::string get_session_id() const { return session_id_; }
    size_t get_message_count() const { return history_.size(); }
    
private:
    static std::string generate_session_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        return "session_" + std::to_string(ms);
    }
    
    static int64_t current_timestamp_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    json create_empi_message(const std::string& task_type, const json& data) const {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        
        // Генерируем message_id: agent-session-timestamp
        std::string message_id = "dialog_" + session_id_ + "_" + std::to_string(ms);
        
        // Для parent_hash: если есть предыдущие сообщения, берем hash последнего
        std::string parent_hash = "";
        if (!history_.empty()) {
            // В реальной реализации нужно вычислять SHA-256
            parent_hash = "prev_" + std::to_string(history_.size() - 1);
        }
        
        return {
            {"header", {
                {"message_id", message_id},
                {"agent_id", "llama_dialog_recorder"},
                {"parent_hash", parent_hash},
                {"timestamp", static_cast<double>(ms) / 1000.0}, // секунды с миллисекундами
                {"protocol_version", "0.1-neuro"},
                {"requires_ack", false},
                {"async_token", "async_" + session_id_}
            }},
            {"payload", {
                {"task_type", task_type},
                {"data", data}
            }}
        };
    }
};

int main(int argc, char ** argv) {
    // ПЕРЕНАПРАВЛЯЕМ STDERR В САМОМ НАЧАЛЕ, КАК В ПРИМЕРЕ
    int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null != -1) {
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
    }

    // Параметры
    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;
    std::string session_id;
    std::string output_file = "llama_dialog_history.json";

    // Парсинг аргументов
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 < argc) {
                model_path = argv[++i];
            } else {
                std::cerr << "Error: Missing model path after -m" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                n_ctx = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: Missing context size after -c" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-ngl") == 0) {
            if (i + 1 < argc) {
                ngl = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: Missing GPU layers count after -ngl" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--session") == 0) {
            if (i + 1 < argc) {
                session_id = argv[++i];
            } else {
                std::cerr << "Error: Missing session ID after --session" << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: Missing output file after --output" << std::endl;
                return 1;
            }
        }
    }

    if (model_path.empty()) {
        std::cerr << "Error: Model path is required (-m /path/to/model.gguf)" << std::endl;
        return 1;
    }

    // Инициализация сборщика истории
    EMPIDialogRecorder dialog_recorder(session_id, output_file);

    // ВЫВОДИМ ЗАГОЛОВОК В stdout (он не перенаправлен)
    std::cout << "=========================================" << std::endl;
    std::cout << "EMPI Dialog Recorder + Llama.cpp" << std::endl;
    std::cout << "Session ID: " << dialog_recorder.get_session_id() << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << std::endl;

    // Загрузка модели - ТОЧНО КАК В РАБОЧЕМ ПРИМЕРЕ
    std::cout << "Loading model: " << model_path << " ..." << std::endl;
    
    ggml_backend_load_all();  // <-- Эта функция работает в вашей версии

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    llama_model * model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model) {
        std::cerr << "Error: Failed to load model" << std::endl;
        return 1;
    }
    std::cout << "Model loaded successfully!" << std::endl;

    const llama_vocab * vocab = llama_model_get_vocab(model);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    ctx_params.n_batch = n_ctx;

    llama_context * ctx = llama_init_from_model(model, ctx_params);  // <-- Используем как в примере
    if (!ctx) {
        std::cerr << "Error: Failed to create context" << std::endl;
        return 1;
    }

    llama_sampler * smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::cout << std::endl;
    std::cout << "Ready for conversation. Type 'quit' to exit." << std::endl;
    std::cout << "=========================================" << std::endl;

    // Основной цикл диалога
    while (true) {
        std::cout << "\n[User] > ";
        std::string user_input;
        std::getline(std::cin, user_input);

        // Выход
        if (user_input.empty() || user_input == "quit" || user_input == "exit") {
            std::cout << "Exiting. History saved to " << output_file << std::endl;
            break;
        }

        // Запись сообщения пользователя
        dialog_recorder.record_user_message(user_input);

        // Подготовка промпта - УПРОЩЕННАЯ ВЕРСИЯ, КАК В ПРИМЕРЕ
        std::string full_prompt = user_input + "\n";

        // Токенизация - ТОЧНО КАК В ПРИМЕРЕ
        std::vector<llama_token> prompt_tokens;
        int n_tokens = llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(), NULL, 0, true, false);
        if (n_tokens < 0) {
            prompt_tokens.resize(-n_tokens);
            llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(),
                          prompt_tokens.data(), prompt_tokens.size(), true, false);
        } else {
            prompt_tokens.resize(n_tokens);
            llama_tokenize(vocab, full_prompt.c_str(), full_prompt.size(),
                          prompt_tokens.data(), prompt_tokens.size(), true, false);
        }

        // Декодирование промпта
        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        if (llama_decode(ctx, batch) != 0) {
            std::cerr << "Error: Decoding failed" << std::endl;
            break;
        }

        // Генерация ответа
        std::cout << "[Assistant] > ";
        std::string assistant_response;
        int max_tokens = 500;

        for (int i = 0; i < max_tokens; i++) {
            llama_token new_token = llama_sampler_sample(smpl, ctx, -1);

            if (llama_vocab_is_eog(vocab, new_token)) {
                break;
            }

            char buf[256];
            int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
            if (n <= 0) {
                break;
            }

            std::string piece(buf, n);

            // Останавливаемся на новой строке (если она не в середине слова)
            if (piece == "\n" && i > 5) {
                break;
            }

            std::cout << piece;
            fflush(stdout);
            assistant_response += piece;

            batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(ctx, batch) != 0) {
                std::cerr << "Error: Decoding failed during generation" << std::endl;
                break;
            }
        }
        std::cout << std::endl;

        // Запись ответа ассистента
        dialog_recorder.record_assistant_message(assistant_response);

        // Показываем статистику
        std::cout << "--- [" << dialog_recorder.get_message_count() / 2
                  << " message pairs recorded]" << std::endl;
    }

    // Сохраняем финальную историю
    dialog_recorder.save_to_file();

    // Также сохраняем упрощенную версию для тестов
    std::string simple_file = "simple_" + output_file;
    std::ofstream simple_out(simple_file);
    simple_out << dialog_recorder.get_simple_history().dump(2);
    std::cout << "Simple history saved to: " << simple_file << std::endl;

    // Очистка - ТОЧНО КАК В ПРИМЕРЕ
    llama_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);

    return 0;
}
