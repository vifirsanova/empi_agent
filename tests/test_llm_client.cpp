#include <iostream>
#include "core/LLMClient.hpp"

int main() {
    EMPI::LLMClient client("python3");

    if (!client.is_available()) {
        std::cerr << "[FAIL] LLMClient not available — llm_client.py not found" << std::endl;
        return 1;
    }

    std::cout << "[OK] LLMClient found script" << std::endl;

    std::string result = client.generate("Say hello in exactly one word.");
    std::cout << "Raw response: " << result << std::endl;

    json parsed = client.generate_json("Say hello in exactly one word.");
    std::cout << "Parsed text: " << parsed.value("text", "no 'text' field") << std::endl;

    return 0;
}
