#pragma once
#include <string>
#include <vector>

// ===== 数据结构 =====

// 一次工具调用请求（来自 LLM 的 tool_calls）
struct ToolCall {
    std::string id;        // 调用 ID，回填结果时要原样带回（tool_call_id）
    std::string name;      // 工具名，如 "calculator"
    std::string arguments; // 参数（JSON 字符串），如 {"expression":"3+5"}
};

// 消息结构体，对应 OpenAI Chat Completions 的 message 格式
struct Message {
    std::string role;    // 角色: "system" / "user" / "assistant" / "tool"
    std::string content; // 消息内容（assistant 发起 tool_calls 时可为空）

    // 仅当 role == "assistant" 且模型决定调用工具时使用
    std::vector<ToolCall> toolCalls;

    // 仅当 role == "tool" 时使用：标记这条结果对应哪次调用
    std::string toolCallId;
};

// 一个工具的规格（从 MCP Server 的 tools/list 动态发现而来）
struct ToolSpec {
    std::string name;        // 工具名
    std::string description; // 工具描述
    std::string inputSchema; // 参数的 JSON Schema（字符串形式）
};

// ===== LLM.cpp =====
// 调用大模型（Function Calling）：传入消息历史 + 可用工具规格，返回一条 assistant 消息
Message callLLM(const std::vector<Message>& messages, const std::vector<ToolSpec>& tools);
