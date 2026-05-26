#include "LLMClient.hpp"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <array>

namespace fs = std::filesystem;

namespace EMPI {

class LLMClient::Impl {
public:
    Impl(const std::string& python_path) 
        : python_path_(python_path)
        , script_path_("integrations/llm_client.py")
        , available_(fs::exists(script_path_))
    {}
    
    /**
     * @brief Calls Python LLM script with a prompt.
     * 
     * Flow: JSON -> temp file -> python3 script < temp_file -> stdout -> return string.
     * max_tokens and temperature are read from agent_config.toml by the Python side.
     */
    std::string generate(const std::string& prompt) {
        if (!available_) {
            return R"({"text": ""})";
        }

        json request = {{"prompt", prompt}};

        // Write JSON to temporary file (popen with "w" doesn't allow reading stdout)
        char temp_path[] = "/tmp/empi_llm_XXXXXX";
        int fd = mkstemp(temp_path);
        if (fd == -1) return R"({"text": ""})";

        std::string json_str = request.dump();
        write(fd, json_str.c_str(), json_str.size());
        close(fd);

        // Pipe stdin from temp file, capture stdout
        std::string cmd = python_path_ + " " + script_path_ + " < " + temp_path;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            unlink(temp_path);
            return R"({"text": ""})";
        }

        // Read response chunk by chunk (buffer is chunk size, not total limit)
        std::string result;
        char buffer[8192];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }

        int status = pclose(pipe);
        unlink(temp_path);

        if (result.empty() || status != 0) {
            return R"({"text": ""})";
        }

        return result;
    }
    
    std::string python_path_;
    std::string script_path_;
    bool available_;
};

LLMClient::LLMClient(const std::string& python_path)
    : impl_(std::make_unique<Impl>(python_path)) {}

LLMClient::~LLMClient() = default;

bool LLMClient::is_available() const {
    return impl_->available_;
}

std::string LLMClient::generate(const std::string& prompt) {
    return impl_->generate(prompt);
}

json LLMClient::generate_json(const std::string& prompt) {
    std::string response = generate(prompt);
    try {
        return json::parse(response);
    } catch (...) {
        // Return raw text if JSON parsing fails (e.g. HTML output from InterfaceGenerator)
        return {{"text", response}};
    }
}

} // namespace EMPI
