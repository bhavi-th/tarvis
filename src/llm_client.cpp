#include "llm_client.h"
#include <curl/curl.h>
#include <iostream>
#include <string>

// Callback for libcurl to capture the HTTP response chunks
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Simple helper to extract a JSON string value by key
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return "";
    pos += search_key.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    
    if (pos < json.length() && json[pos] == '"') {
        pos++; // skip opening quote
        size_t end_pos = json.find('"', pos);
        if (end_pos != std::string::npos) {
            return json.substr(pos, end_pos - pos);
        }
    }
    return "";
}

// Simple helper to extract a JSON numeric value by key
int extract_json_int(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return 0;
    pos += search_key.length();
    
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    
    size_t end_pos = pos;
    while (end_pos < json.length() && (isdigit(json[end_pos]) || json[end_pos] == '.')) {
        end_pos++;
    }
    
    if (end_pos > pos) {
        try {
            return std::stoi(json.substr(pos, end_pos - pos));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string query_lm_studio(const std::string& prompt) {
    CURL* curl;
    CURLcode res;
    std::string response_string;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:1234/v1/chat/completions");
        
        std::string json_payload = "{\n"
            "  \"model\": \"local-model\",\n"
            "  \"messages\": [\n"
            "    {\"role\": \"system\", \"content\": \"You are TARVIS, a concise and sharp local AI assistant.\"},\n"
            "    {\"role\": \"user\", \"content\": \"" + prompt + "\"}\n"
            "  ],\n"
            "  \"temperature\": 0.7\n"
            "}";

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[Tarvis Error] curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return "[TARVIS AI]: (error) Connection failed (0 tokens)";
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    // Parse the fields from the JSON response
    std::string model = extract_json_string(response_string, "model");
    std::string content = extract_json_string(response_string, "content");
    int total_tokens = extract_json_int(response_string, "total_tokens");

    if (model.empty()) model = "unknown-model";
    if (content.empty()) content = response_string; // Fallback if parsing fails

    // Format output as requested: [TARVIS AI]: (model) content (total_tokens)
    char formatted_output[2048];
    std::snprintf(formatted_output, sizeof(formatted_output), "[TARVIS AI]: (%s) %s (%d tokens)", 
                  model.c_str(), content.c_str(), total_tokens);

    return std::string(formatted_output);
}
