#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

// 消息结构体，对应 OpenAI API 的 message 格式
struct Message {
    std::string role;    // 角色: "system" / "user" / "assistant"
    std::string content; // 消息内容
};

// 工具结构体，Agent 可调用的外部能力
struct Tool {
    std::string name;        // 工具名称，如 "calculator"
    std::string description; // 工具描述，会放进 system prompt 让 LLM 知道
    std::function<std::string(std::string)> execute; // 实际执行函数
};

// LLM 输出解析结果
struct ParsedOutput {
    std::string thought;     // 思考过程
    std::string action;      // 要调用的工具名（为空表示没有 action）
    std::string actionInput; // 工具入参
    std::string finalAnswer; // 最终答案（为空表示还没结束）
};

// ===== memory.cpp =====
void addMessage(const std::string& role, const std::string& content); // 追加消息到历史
std::vector<Message>& getHistory(); // 获取全部消息历史
void clearHistory(); // 清空历史（每轮新对话前调用）

// ===== Tool.cpp =====
void registerTool(const std::string& name, const std::string& description,
                  std::function<std::string(std::string)> func); // 注册工具
std::string callTool(const std::string& name, const std::string& input); // 根据名称调用工具
std::string getToolsDescription(); // 获取所有工具描述，用于拼接 system prompt

// ===== LLM.cpp =====
std::string callLLM(const std::vector<Message>& messages); // 调用大模型，返回文本回复

// ===== praseout.cpp =====
ParsedOutput parseOutput(const std::string& text); // 解析 LLM 输出，提取 Action 或 Final Answer
