#include <iostream>
#include <fstream>
#include <memory>
#include "core/LLMClient.hpp"
#include "agents/TextAnalyzer.hpp"
#include "agents/FeedbackAgent.hpp"
#include "agents/InterfaceGenerator.hpp"

using json = nlohmann::json;

int main() {
    auto llm = std::make_shared<EMPI::LLMClient>("python3");

    EMPI::TextAnalyzer text_agent;
    EMPI::FeedbackAgent feedback_agent(llm);
    EMPI::InterfaceGenerator interface_gen(llm, "");

    std::string sample_text =
        "The water cycle describes the continuous movement of water on, above, "
        "and below the surface of the Earth. This process involves evaporation, "
        "condensation, precipitation, and collection. Water evaporates from oceans "
        "and lakes, rises into the atmosphere, condenses into clouds, and falls "
        "back to Earth as rain or snow. The cycle is essential for life on Earth "
        "and influences climate, weather, and ecosystems.";

    json dialog = json::array({
        {{"role", "user"}, {"content", "I have ADHD and struggle with long paragraphs. Can you simplify this?"}},
        {{"role", "assistant"}, {"content", "I will break the text into short paragraphs and highlight key points."}}
    });

    std::cout << "=== EMPI Pipeline Test ===" << std::endl;
    std::cout << "Input: " << sample_text.size() << " chars, " << dialog.size() << " dialog messages" << std::endl;

    // Step 1: TextAnalyzer
    json ta_result = text_agent.process_raw({{"text", sample_text}});
    json metrics = ta_result["payload"]["data"];
    std::cout << "TextAnalyzer: " << (metrics["status"] == "success" ? "OK" : "FAIL") << std::endl;

    // Step 2: FeedbackAgent
    json fa_result = feedback_agent.process_raw({{"dialog_history", dialog}});
    json feedback = fa_result["payload"]["data"];
    std::cout << "FeedbackAgent: " << (feedback["status"] == "success" ? "OK" : "FAIL") << std::endl;

    // Step 3: InterfaceGenerator
    json ig_input = {
        {"text_metrics", metrics.value("metrics", json::object())},
        {"feedback_analysis", feedback.value("analysis", json::object())},
        {"original_text", sample_text}
    };
    json ig_result = interface_gen.process_raw(ig_input);
    json html_data = ig_result["payload"]["data"];

    if (html_data["status"] == "success") {
        std::ofstream file("test_output.html");
        file << html_data["html"].get<std::string>();
        file.close();
        std::cout << "InterfaceGenerator: OK" << std::endl;
        std::cout << "Output: build/test_output.html (" << html_data["html_size"] << " bytes)" << std::endl;
    } else {
        std::cerr << "InterfaceGenerator: FAIL — " << html_data.value("message", "unknown") << std::endl;
        return 1;
    }

    std::cout << "=== Pipeline complete ===" << std::endl;
    return 0;
}
