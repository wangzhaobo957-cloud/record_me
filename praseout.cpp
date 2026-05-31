// praseout.cpp - 解析 LLM 输出
// 从 LLM 返回的文本中，按固定前缀提取 Thought / Action / Final Answer
#include "agent.h"

// 从文本中提取指定前缀后面的那一行内容（去掉前后空格）
static std::string extractLine(const std::string& text, const std::string& prefix) {
    size_t pos = text.find(prefix);
    if (pos == std::string::npos) return "";
    size_t start = pos + prefix.size();
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string result = text.substr(start, end - start);
    // 去掉前后空白
    while (!result.empty() && result.front() == ' ') result.erase(0, 1);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\r')) result.pop_back();
    return result;
}

// 解析 LLM 输出文本，返回结构化结果
ParsedOutput parseOutput(const std::string& text) {
    ParsedOutput out;
    out.thought = extractLine(text, "Thought:");
    out.action = extractLine(text, "Action:");
    out.actionInput = extractLine(text, "Action Input:");
    out.finalAnswer = extractLine(text, "Final Answer:");
    return out;
}
