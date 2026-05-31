// main.cpp - ReAct Agent 主循环
// 流程：用户输入 → 拼接 system prompt → 进入循环（调 LLM → 解析 → 调工具 → 追加历史） → 输出最终答案
#include "agent.h"
#include <iostream>

// System Prompt 模板，%s 会被替换为工具描述
static const char* SYSTEM_PROMPT_TEMPLATE = R"(You are a helpful assistant. You have access to the following tools:

%s

To use a tool, you MUST use the following format:

Thought: <your reasoning about what to do>
Action: <tool name>
Action Input: <input to the tool>

When you have enough information to answer the question, use:

Thought: <your reasoning>
Final Answer: <your final answer>

Begin!)";

// 构建 system prompt：把工具描述填进模板
static std::string buildSystemPrompt() {
    std::string toolsDesc = getToolsDescription();
    char buf[2048];
    snprintf(buf, sizeof(buf), SYSTEM_PROMPT_TEMPLATE, toolsDesc.c_str());
    return std::string(buf);
}

int main() {
    // ===== 注册工具 =====

    // 计算器工具：通过系统 bc 命令执行数学表达式
    registerTool("calculator", "Useful for math calculations. Input should be a math expression like '3+5'.",
        [](std::string input) -> std::string {
            FILE* pipe = popen(("echo '" + input + "' | bc").c_str(), "r");
            if (!pipe) return "Error";
            char buffer[128];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
            pclose(pipe);
            while (!result.empty() && result.back() == '\n') result.pop_back();
            return result;
        });

    // 搜索工具：模拟搜索（可替换为真实搜索 API）
    registerTool("search", "Useful for looking up current information. Input should be a search query.",
        [](std::string input) -> std::string {
            return "Search result: The capital of France is Paris. It has a population of about 2.1 million.";
        });

    // ===== 主交互循环 =====
    std::cout << "=== Mini ReAct Agent ===" << std::endl;
    std::cout << "Enter your question (or 'quit' to exit):" << std::endl;
        //用户输入的问题
    std::string userInput;
    // 主交互循环：用户输入 → 拼接 system prompt → 进入 ReAct 循环 → 输出最终答案
    while (true) {
        std::cout << "\n> ";
        //输入的问题
        std::getline(std::cin, userInput);
        if (userInput == "quit") break;
        if (userInput.empty()) continue;

        // 每轮新问题：清空历史，重新拼接 system prompt + 用户问题
        clearHistory();
        //本质是一个vector, 每个vector都是一个Message, 包含role和 content字段
        //struct Message {
        //    std::string role;    // 角色: "system" / "user" / "assistant"
        //    std::string content; // 消息内容
        //};
        //把提示词放进去
        addMessage("system", buildSystemPrompt());
        //把用户的问题放进去
        addMessage("user", userInput);

        // ===== ReAct 循环 =====
        const int MAX_ITERATIONS = 10; // 最大迭代次数，防止死循环
        for (int i = 0; i < MAX_ITERATIONS; i++) {
            // 1. 调用 LLM
            std::string response = callLLM(getHistory());
            std::cout << "\n[LLM]: " << response << std::endl;

            // 2. LLM 回复追加到历史
            addMessage("assistant", response);

            // 3. 解析输出
            ParsedOutput parsed = parseOutput(response);

            // 4. 如果有最终答案 → 输出并结束本轮
            if (!parsed.finalAnswer.empty()) {
                std::cout << "\n✅ Answer: " << parsed.finalAnswer << std::endl;
                break;
            }

            // 5. 如果有 Action → 调用工具，把结果追加到历史
            if (!parsed.action.empty()) {
                std::cout << "\n🔧 Calling tool: " << parsed.action
                          << "(" << parsed.actionInput << ")" << std::endl;
                std::string observation = callTool(parsed.action, parsed.actionInput);
                std::cout << "📎 Observation: " << observation << std::endl;
                addMessage("user", "Observation: " + observation);
            } else {
                // 既没有 Action 也没有 Final Answer，提示 LLM 遵循格式
                std::cout << "\n⚠️  No action or final answer found, retrying..." << std::endl;
                addMessage("user", "Please follow the format: use 'Action:' to call a tool, or 'Final Answer:' to give your answer.");
            }

            if (i == MAX_ITERATIONS - 1) {
                std::cout << "\n❌ Max iterations reached." << std::endl;
            }
        }
    }

    std::cout << "Bye!" << std::endl;
    return 0;
}
