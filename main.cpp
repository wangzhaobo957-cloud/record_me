// main.cpp - ReAct 雏形
// ReAct = Reason(思考) -> Act(行动/调工具) -> Observe(观察结果) 的循环，
// 直到模型不再调用工具、给出最终答案为止。
// 底层完全复用 LLM.cpp 里的 callLLM（原生 Function Calling）。
#include "agent.h"
#include "json.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using json = nlohmann::json;

// ===== 一个真实可跑的工具：计算器（支持 + - * / 和括号）=====
// 这是 ReAct 里模型"行动(Act)"能调用的动作之一。
namespace calc {
    static const char* p;
    static double expr();
    static double factor() {
        while (*p == ' ') ++p;
        if (*p == '(') {
            ++p;
            double v = expr();
            while (*p == ' ') ++p;
            if (*p == ')') ++p;
            return v;
        }
        char* end;
        double v = std::strtod(p, &end);
        p = end;
        return v;
    }
    static double term() {
        double v = factor();
        while (true) {
            while (*p == ' ') ++p;
            if (*p == '*') { ++p; v *= factor(); }
            else if (*p == '/') { ++p; v /= factor(); }
            else break;
        }
        return v;
    }
    static double expr() {
        double v = term();
        while (true) {
            while (*p == ' ') ++p;
            if (*p == '+') { ++p; v += term(); }
            else if (*p == '-') { ++p; v -= term(); }
            else break;
        }
        return v;
    }
    static double eval(const std::string& s) { p = s.c_str(); return expr(); }
}

// 执行某个工具调用，返回观察结果（Observation）字符串
static std::string executeTool(const std::string& name, const std::string& args) {
    if (name == "calculator") {
        json a = json::parse(args, nullptr, false);
        if (a.is_discarded() || !a.contains("expression"))
            return "工具参数错误：缺少 expression";
        std::string e = a["expression"].get<std::string>();
        return e + " = " + std::to_string(calc::eval(e));
    }
    return "未知工具: " + name;
}

// 声明可供模型"行动"的工具清单（动作空间）
static std::vector<ToolSpec> buildTools() {
    ToolSpec spec;
    spec.name = "calculator";
    spec.description = "计算一个数学表达式，支持 + - * / 和括号";
    spec.inputSchema = R"({
        "type": "object",
        "properties": {
            "expression": {"type": "string", "description": "要计算的表达式，如 (3+5)*12"}
        },
        "required": ["expression"]
    })";
    return { spec };
}

// ReAct 智能体：把 thought/use/observe 三个方法对应到 ReAct 三个阶段
class agent {
public:
    // 构造函数：初始化智能体，设置系统提示和工具清单
    agent(std::string systemPrompt, std::vector<ToolSpec> tools)
        : tools_(std::move(tools)) {
        Message sys; sys.role = "system"; sys.content = std::move(systemPrompt);
        messages_.push_back(sys);//系统提示词塞入到vector中 ，作为第一个消息
    }

    // 跑一个任务：ReAct 主循环
    std::string run(const std::string& task, int maxSteps = 5) {
        Message usr; usr.role = "user"; usr.content = task;
        messages_.push_back(usr);

        for (int step = 0; step < maxSteps; ++step) {
            std::cout << "\n===== Step " << (step + 1) << " =====\n";

            // 1) Reason：让模型思考下一步（回答 or 调工具）
            Message reply = thought();
            messages_.push_back(reply);

            // 没有工具调用 => 模型给出了最终答案，循环收敛
            if (reply.toolCalls.empty()) {
                std::cout << "[Final] " << reply.content << "\n";
                return reply.content;
            }

            // 2) Act + 3) Observe：逐个执行工具，把结果回填历史
            for (const auto& call : reply.toolCalls) {
                std::string observation = use(call);
                observe(call, observation);
            }
        }
        return "（已达最大步数，未收敛）";
    }

private:
    // 思考：调模型，由它决定"回答"还是"发起工具调用"
    Message thought() {
        return callLLM(messages_, tools_);
    }

    // 使用：执行一次工具调用，返回观察结果
    std::string use(const ToolCall& call) {
        std::cout << "[Act]     " << call.name << " " << call.arguments << "\n";
        return executeTool(call.name, call.arguments);
    }

    // 观察：把工具结果作为 tool 消息加回历史，供下一轮思考
    void observe(const ToolCall& call, const std::string& observation) {
        std::cout << "[Observe] " << observation << "\n";
        Message m; m.role = "tool"; m.toolCallId = call.id; m.content = observation;
        messages_.push_back(m);
    }

    std::vector<Message> messages_; // 完整对话历史（含 tool 结果）
    std::vector<ToolSpec> tools_;   // 可用工具（动作空间）
};

int main() {
    agent bot(
        "你是一个会使用工具的智能体。需要数学计算时，必须调用 calculator 工具，不要自己心算。",
        buildTools()
    );

    bot.run("帮我算一下 (3 + 5) * 12 再减去 6 等于多少？");
    return 0;
}
