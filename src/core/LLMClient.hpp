#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace EMPI {

class LLMClient {
public:
    explicit LLMClient(const std::string& python_path = "python3");
    ~LLMClient();
    
    bool is_available() const;
    
    std::string generate(const std::string& prompt);
    json generate_json(const std::string& prompt);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
};

} // namespace EMPI
