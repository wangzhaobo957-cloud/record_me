// LLM.cpp - 调用大模型 API
// 使用阿里云 Dashscope（通义千问）接口，把消息历史序列化成 JSON，通过 HTTP POST 发送
#include "agent.h"
#include "json.hpp"
#include <curl/curl.h>

using json = nlohmann::json;

static const char* API_URL = "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
static const char* API_KEY = "sk-2df881fc84684478a9cc28f77714ff2b";
static const char* MODEL = "qwen-turbo";

// libcurl 写回调：把 HTTP 响应体追加到 string 中
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// 调用大模型，传入完整消息历史，返回 LLM 的文本回复
std::string callLLM(const std::vector<Message>& messages) {
    // 构建 messages JSON 数组
    json messagesJson = json::array();
    for (auto& msg : messages) {
        messagesJson.push_back({{"role", msg.role}, {"content", msg.content}});
    }

    // 构建 Dashscope 格式的请求体
    json body;
    body["model"] = MODEL;
    body["input"]["messages"] = messagesJson;
    body["parameters"]["result_format"] = "message";

    std::string bodyStr = body.dump();
    std::string response;

    // 初始化 libcurl
    CURL* curl = curl_easy_init();
    if (!curl) return "Error: curl init failed";

    // 设置请求头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string authHeader = std::string("Authorization: Bearer ") + API_KEY;
    headers = curl_slist_append(headers, authHeader.c_str());

    // 配置 curl 参数并发送请求
    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "Error: " + std::string(curl_easy_strerror(res));
    }

    // 解析 Dashscope 响应格式：output.choices[0].message.content
    json resJson = json::parse(response, nullptr, false);
    if (resJson.is_discarded()) {
        return "Error: JSON parse failed - " + response;
    }
    if (resJson.contains("output") &&
        resJson["output"].contains("choices") &&
        !resJson["output"]["choices"].empty()) {
        return resJson["output"]["choices"][0]["message"]["content"].get<std::string>();
    }

    return "Error: unexpected response - " + response;
}
