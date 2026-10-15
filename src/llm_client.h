#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>

// Sends a prompt to the local LM Studio server and returns the response
std::string query_lm_studio(const std::string& prompt);

#endif
