// memory.cpp - 内存版消息历史管理
// 用一个 vector 存储整轮对话的所有消息，每次新问题清空重来
#include "agent.h"

static std::vector<Message> history; // 全局消息历史

// 追加一条完整消息（可携带 tool_calls / tool_call_id）
void addMessage(const Message& msg) {
    history.push_back(msg);
}

// 便捷重载：只有 role + content 的简单消息
void addMessage(const std::string& role, const std::string& content) {
    history.push_back({role, content, {}, ""});
}

// 返回当前所有消息历史的引用，用于发给 LLM
std::vector<Message>& getHistory() {
    return history;
}

// 清空消息历史，每轮新问题前调用
void clearHistory() {
    history.clear();
}
