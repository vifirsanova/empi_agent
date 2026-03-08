#include "llama.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>

int main() {
    llama_backend_init();
    
    // Параметры модели (как в твоём InterfaceGenerator)
    auto model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;
    
    llama_model* model = llama_model_load_from_file(
        "../llama-dynamic-context/models/Phi-3-mini-4k-instruct-q4.gguf", 
        model_params
    );
    
    auto ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 4096;
    ctx_params.n_batch = 512;
    ctx_params.n_threads = std::thread::hardware_concurrency();
    
    llama_context* ctx = llama_init_from_model(model, ctx_params);
    
    // Промпт как в твоём construct_prompt
    std::string prompt = 
        "<|user|>\n"
        "Generate a clean HTML interface with accessibility features.\n"
        "The user has dyslexia: use OpenDyslexic font, cream background.\n"
        "Text: 'The water cycle describes the movement of water on Earth.'\n"
        "<|assistant|>\n";
    
    // Токенизация
    std::vector<llama_token> tokens(4096);
    int n_tokens = llama_tokenize(
        llama_model_get_vocab(model),
        prompt.c_str(),
        prompt.length(),
        tokens.data(),
        tokens.size(),
        true,  // add_bos
        true   // special tokens
    );
    tokens.resize(n_tokens);
    
    auto batch = llama_batch_get_one(tokens.data(), tokens.size());
    llama_decode(ctx, batch);
    
    auto sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    
    for (int i = 0; i < 500; i++) {
        llama_token new_token = llama_sampler_sample(sampler, ctx, -1);
        
        if (llama_vocab_is_eog(llama_model_get_vocab(model), new_token))
            break;
        
        char buf[256];
        int n = llama_token_to_piece(
            llama_model_get_vocab(model), 
            new_token, 
            buf, 
            sizeof(buf), 
            0, 
            true
        );
        std::cout << std::string(buf, n);
        
        batch = llama_batch_get_one(&new_token, 1);
        llama_decode(ctx, batch);
    }
    
    // Cleanup
    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
}
