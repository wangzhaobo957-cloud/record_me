// main.cpp - ReAct Agent 主循环（MCP + Function Calling 版本）
// 流程：
//   启动时：拉起 MCP Server 子进程 → 握手 → tools/list 动态发现工具
//   每轮对话：用户输入 → 循环[ 调 LLM(带 tools) → 若有 tool_calls 则经 MCP 执行 → 回填 tool 消息 ] → 输出最终答案
#include "agent.h"
#include <iostream>

int main() {
    // ===== 启动 MCP Server 并发现工具 =====
    // 工具不再编译在本程序里，而是住在独立的 mcp_server 进程中。
    if (!mcpStart("./mcp_server")) {
        std::cerr << "Error: failed to start MCP server (./mcp_server)" << std::endl;
        return 1;
    }
    std::cout << "✅ MCP server started." << std::endl;

    // tools/list：动态发现工具（取代了原来写死的工具描述）
    std::vector<ToolSpec> tools = mcpListTools();
    std::cout << "🔧 Discovered " << tools.size() << " tools via MCP:" << std::endl;
    for (auto& t : tools) {
        std::cout << "   - " << t.name << ": " << t.description << std::endl;
    }

    // ===== 主交互循环 =====
    std::cout << "\n=== Mini ReAct Agent (MCP + Function Calling) ===" << std::endl;
    std::cout << "Enter your question (or 'quit' to exit):" << std::endl;

    std::string userInput;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, userInput);
        if (userInput == "quit") break;
        if (userInput.empty()) continue;

        // 每轮新问题：清空历史，给一个极简 system + 用户问题
        // 注意：不再需要冗长的格式约束 prompt，工具走 tools 字段。
        clearHistory();
        addMessage("system", "You are a helpful assistant. Use the provided tools when needed.");
        addMessage("user", userInput);

        // ===== ReAct 循环 =====
        const int MAX_ITERATIONS = 10;
        for (int i = 0; i < MAX_ITERATIONS; i++) {
            std::cout << "\n===== 第 " << (i + 1) << " 轮 =====" << std::endl;

            // 1. 调用 LLM（带上动态发现的 tools）
            Message reply = callLLM(getHistory(), tools);

            // 2. 把 assistant 消息追加到历史（可能含 tool_calls）
            addMessage(reply);

            // 3. 没有工具调用 → 这是最终答案，输出并结束本轮
            if (reply.toolCalls.empty()) {
                std::cout << "\n✅ Answer: " << reply.content << std::endl;
                break;
            }

            // 4. 有工具调用 → 逐个经 MCP 执行，结果以 role=tool 回填
            std::cout << "[LLM] requested " << reply.toolCalls.size() << " tool call(s)" << std::endl;
            for (auto& call : reply.toolCalls) {
                std::cout << "🔧 tools/call → " << call.name
                          << " args=" << call.arguments << std::endl;

                std::string observation = mcpCallTool(call.name, call.arguments);
                std::cout << "📎 result: " << observation << std::endl;

                // 回填工具结果：role=tool，必须带上对应的 tool_call_id
                Message toolMsg;
                toolMsg.role = "tool";
                toolMsg.content = observation;
                toolMsg.toolCallId = call.id;
                addMessage(toolMsg);
            }

            if (i == MAX_ITERATIONS - 1) {
                std::cout << "\n❌ Max iterations reached." << std::endl;
            }
        }
    }

    // ===== 收尾：关闭 MCP Server 子进程 =====
    mcpStop();
    std::cout << "Bye!" << std::endl;
    return 0;
}
