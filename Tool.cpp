// Tool.cpp - 工具注册与调用
// Agent 的"手脚"：定义工具、注册工具、按名称执行工具
#include "agent.h"
#include <iostream>

static std::map<std::string, Tool> tools; // 全局工具注册表

// 注册一个工具：名称 + 描述 + 执行函数
void registerTool(const std::string& name, const std::string& description,
                  std::function<std::string(std::string)> func) {
    tools[name] = {name, description, func};
}

// 根据工具名称调用对应工具，返回执行结果
std::string callTool(const std::string& name, const std::string& input) {
    if (tools.find(name) == tools.end()) {
        return "Error: tool '" + name + "' not found";
    }
    return tools[name].execute(input);
}

// 拼接所有工具的描述信息，塞进 system prompt 让 LLM 知道有哪些工具可用
std::string getToolsDescription() {
    std::string desc;
    for (auto& [name, tool] : tools) {
        desc += "- " + tool.name + ": " + tool.description + "\n";
    }
    return desc;
}
