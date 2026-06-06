// LLM.cpp - 调用大模型（Function Calling 版本）
// 改用 OpenAI 兼容端点（Dashscope compatible-mode），原生支持 tools / tool_calls。
// 不再需要在 prompt 里手写格式约束，也不再需要字符串解析输出。
#include "agent.h"
#include "json.hpp"
#include <curl/curl.h>
#include <cstdlib>

using json = nlohmann::json;

// OpenAI 兼容端点（Dashscope）
static const char* API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
static const char* MODEL = "qwen-turbo";

// libcurl 写回调：把 HTTP 响应体追加到 string
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// 把一条 Message 转成 OpenAI 格式的 JSON
static json messageToJson(const Message& msg) {
    json m;
    m["role"] = msg.role;

    if (msg.role == "tool") {
        // 工具执行结果：必须带 tool_call_id
        m["content"] = msg.content;
        m["tool_call_id"] = msg.toolCallId;
        return m;
    }

    if (msg.role == "assistant" && !msg.toolCalls.empty()) {
        // 模型发起工具调用的消息：content 可为 null，带 tool_calls
        m["content"] = msg.content.empty() ? json(nullptr) : json(msg.content);
        json calls = json::array();
        for (auto& tc : msg.toolCalls) {
            calls.push_back({
                {"id", tc.id},
                {"type", "function"},
                {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}
            });
        }
        m["tool_calls"] = calls;
        return m;
    }

    m["content"] = msg.content;
    return m;
}

// 把 ToolSpec 列表转成请求里的 tools 字段
static json toolsToJson(const std::vector<ToolSpec>& tools) {
    json arr = json::array();
    for (auto& t : tools) {
        json params = json::parse(t.inputSchema, nullptr, false);
        if (params.is_discarded()) params = json::object();
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name", t.name},
                {"description", t.description},
                {"parameters", params}
            }}
        });
    }
    return arr;
}

// 调用大模型，传入消息历史 + 可用工具，返回一条 assistant 消息
Message callLLM(const std::vector<Message>& messages, const std::vector<ToolSpec>& tools) {
    Message result;
    result.role = "assistant";

    const char* apiKey = std::getenv("DASHSCOPE_API_KEY");
    if (!apiKey) {
        result.content = "Error: environment variable DASHSCOPE_API_KEY is not set";
        return result;
    }

    // 构建请求体
    json body;
    body["model"] = MODEL;
    json msgArr = json::array();
    for (auto& msg : messages) msgArr.push_back(messageToJson(msg));
    body["messages"] = msgArr;
    if (!tools.empty()) body["tools"] = toolsToJson(tools);

    std::string bodyStr = body.dump();
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) { result.content = "Error: curl init failed"; return result; }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string authHeader = std::string("Authorization: Bearer ") + apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.content = "Error: " + std::string(curl_easy_strerror(res));
        return result;
    }

    // 解析 OpenAI 格式响应：choices[0].message
    json resJson = json::parse(response, nullptr, false);
    if (resJson.is_discarded()) {
        result.content = "Error: JSON parse failed - " + response;
        return result;
    }
    if (!resJson.contains("choices") || resJson["choices"].empty()) {
        result.content = "Error: unexpected response - " + response;
        return result;
    }

    json message = resJson["choices"][0]["message"];

    // 普通文本回复
    if (message.contains("content") && !message["content"].is_null()) {
        result.content = message["content"].get<std::string>();
    }

    // 工具调用：解析 tool_calls
    if (message.contains("tool_calls") && !message["tool_calls"].is_null()) {
        for (auto& tc : message["tool_calls"]) {
            ToolCall call;
            call.id = tc.value("id", "");
            if (tc.contains("function")) {
                call.name = tc["function"].value("name", "");
                call.arguments = tc["function"].value("arguments", "{}");
            }
            result.toolCalls.push_back(call);
        }
    }

    return result;
}
