#include "llm_client.h"
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <functional>

struct StreamContext {
    std::string* full_response;
    std::function<void(const std::string&)>* chunk_callback;
};

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string chunk((char*)contents, total_size);
    StreamContext* context = static_cast<StreamContext*>(userp);
    
    size_t pos = 0;
    while ((pos = chunk.find("data: ", pos)) != std::string::npos) {
        pos += 6;
        size_t end_line = chunk.find('\n', pos);
        if (end_line == std::string::npos) end_line = chunk.length();
        
        std::string json_line = chunk.substr(pos, end_line - pos);
        if (json_line == "[DONE]") break;
        
        size_t content_pos = json_line.find("\"content\":");
        if (content_pos != std::string::npos) {
            // Move pointer to the start of the value after "content":
            content_pos += 10; 
            
            // Find the first quote that starts the actual string value
            size_t start_quote = json_line.find('"', content_pos);
            if (start_quote != std::string::npos) {
                size_t end_quote = json_line.find('"', start_quote + 1);
                if (end_quote != std::string::npos) {
                    std::string token = json_line.substr(start_quote + 1, end_quote - (start_quote + 1));
                    
                    // Accumulate full response
                    if (context && context->full_response) {
                        *(context->full_response) += token;
                    }
                    
                    // Fire callback chunk to main.cpp
                    if (context && context->chunk_callback && *(context->chunk_callback)) {
                        (*(context->chunk_callback))(token);
                    }
                }
            }
        }
        pos = end_line;
    }

    return total_size;
}

std::string query_lm_studio(const std::string& prompt, std::function<void(const std::string&)> on_chunk) {
    CURL* curl;
    CURLcode res;
    std::string full_response = "";
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:1234/v1/chat/completions");
        
        std::string json_payload = "{\n"
            "  \"model\": \"local-model\",\n"
            "  \"messages\": [\n"
            "    {\"role\": \"system\", \"content\": \"You are TARVIS, a concise and sharp local AI assistant. TARVIS stands for Terminal Autonomous Reliable Voice Integrated System.Always ensure proper spacing between words and sentences.\"},\n"
            "    {\"role\": \"user\", \"content\": \"" + prompt + "\"}\n"
            "  ],\n"
            "  \"temperature\": 0.7,\n"
            "  \"stream\": true\n"
            "}";

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        StreamContext context{&full_response, &on_chunk};

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "\n[Tarvis Error] curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return "(error) Connection failed";
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    return full_response;
}
