// mcp_server.cpp - 一个最简 MCP Server（独立可执行程序）
// 传输方式：JSON-RPC 2.0 over stdio（按行分隔，一行一条 JSON 消息）
// 它和 Agent 是两个独立进程，通过管道通信——这就是真实 MCP 的形态。
// 支持的方法：initialize / tools/list / tools/call
#include "json.hpp"
#include <iostream>
#include <string>
#include <cstdio>

using json = nlohmann::json;

// 工具实现：计算器（调用系统 bc 命令）
static std::string toolCalculator(const std::string& expression) {
    FILE* pipe = popen(("echo '" + expression + "' | bc").c_str(), "r");
    if (!pipe) return "Error: popen failed";
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
    pclose(pipe);
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result.empty() ? "Error: invalid expression" : result;
}

// 工具实现：搜索（这里用硬编码模拟，可替换为真实搜索 API）
static std::string toolSearch(const std::string& query) {
    return "Search result for '" + query +
           "': The capital of France is Paris. It has a population of about 2140000.";
}

// 构造 tools/list 返回的工具清单（每个工具带结构化 inputSchema）
static json buildToolsList() {
    json calc;
    calc["name"] = "calculator";
    calc["description"] = "Evaluate a math expression and return the numeric result.";
    calc["inputSchema"] = {
        {"type", "object"},
        {"properties", {
            {"expression", {{"type", "string"}, {"description", "a math expression, e.g. 3+5*2"}}}
        }},
        // 限制：expression 是必填项
        {"required", json::array({"expression"})}
    };

    json search;
    search["name"] = "search";
    search["description"] = "Look up current information by a query string.";
    search["inputSchema"] = {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}, {"description", "the search query"}}}
        }},
        // 限制：query 是必填项
        {"required", json::array({"query"})}
    };

    return json::array({calc, search});
}

// 发送一条 JSON-RPC 响应（写一行到 stdout 并 flush）
static void sendResponse(const json& resp) {
    std::cout << resp.dump() << std::endl;
    std::cout.flush();
}

int main() {
    std::string line;
    // 主循环：每读到一行 JSON-RPC 请求就处理一条
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json req = json::parse(line, nullptr, false);
        if (req.is_discarded()) continue; // 非法 JSON，忽略

        std::string method = req.value("method", "");
        json id = req.contains("id") ? req["id"] : json(nullptr);

        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;

        if (method == "initialize") {
            // 握手：返回协议版本和能力声明
            resp["result"] = {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "mini-mcp-server"}, {"version", "1.0.0"}}}
            };
            sendResponse(resp);
        } else if (method == "tools/list") {
            // 工具发现：返回工具清单
            resp["result"] = {{"tools", buildToolsList()}};
            sendResponse(resp);
        } else if (method == "tools/call") {
            // 工具执行：根据 name 分发，从 arguments 取参数
            json params = req.value("params", json::object());
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());

            std::string output;
            if (name == "calculator") {
                output = toolCalculator(args.value("expression", ""));
            } else if (name == "search") {
                output = toolSearch(args.value("query", ""));
            } else {
                output = "Error: unknown tool '" + name + "'";
            }

            // MCP 规定结果放在 content 数组里，每项有 type
            resp["result"] = {
                {"content", json::array({{{"type", "text"}, {"text", output}}})}
            };
            sendResponse(resp);
        } else if (method == "notifications/initialized") {
            // 通知类消息没有 id，不需要回复
            continue;
        } else {
            // 未知方法：返回 JSON-RPC 标准错误
            resp["error"] = {{"code", -32601}, {"message", "Method not found: " + method}};
            sendResponse(resp);
        }
    }
    return 0;
}
