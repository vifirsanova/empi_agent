#include <iostream>
#include <fstream>
#include <memory>
#include "core/LLMClient.hpp"
#include "agents/InterfaceGenerator.hpp"

int main() {
    std::string local_model;
    std::ifstream config_file("config/agent_config.toml");
    if (config_file.is_open()) {
        std::string line;
        while (std::getline(config_file, line)) {
            if (line.find("local_model_path") != std::string::npos) {
                auto start = line.find('"');
                auto end = line.rfind('"');
                if (start != std::string::npos && end != std::string::npos && start != end) {
                    local_model = line.substr(start + 1, end - start - 1);
                }
                break;
            }
        }
    }

    auto llm = std::make_shared<EMPI::LLMClient>("python3");
    EMPI::InterfaceGenerator gen(llm, local_model);

    nlohmann::json metrics = {
        {"flesch_kincaid_grade", 10.5},
        {"flesch_reading_ease", 55.2},
        {"complexity_label", "moderate"}
    };

    nlohmann::json feedback = {
        {"sentiment", "neutral"},
        {"topics", {"ADHD", "focus"}},
        {"satisfaction_score", 0.6},
        {"complaints", {"long paragraphs"}},
        {"feedback_summary", "User has ADHD and struggles with long paragraphs"}
    };

    nlohmann::json input = {
        {"text_metrics", metrics},
        {"feedback_analysis", feedback},
        {"original_text", "The water cycle describes the continuous movement of water on, above, and below the surface of the Earth. This process involves evaporation, condensation, precipitation, and collection. Water evaporates from oceans and lakes, rises into the atmosphere, condenses into clouds, and falls back to Earth as rain or snow."}
    };

    std::cout << "Local model: " << (local_model.empty() ? "none" : local_model) << std::endl;
    std::cout << "Generating HTML..." << std::endl;

    nlohmann::json result = gen.process_raw(input);
    nlohmann::json data = result["payload"]["data"];

    if (data["status"] == "success") {
        std::string html = data["html"].get<std::string>();
        std::cout << "Status: success, size: " << data["html_size"] << " bytes" << std::endl;
        std::ofstream file("index.html");
        file << html;
        file.close();
        std::cout << "Saved to: build/index.html" << std::endl;
    } else {
        std::cerr << "Error: " << data.value("message", "unknown") << std::endl;
        return 1;
    }

    return 0;
}
