// mcp_client.cpp - 最简 MCP Client
// 职责：拉起 MCP Server 子进程，通过管道用 JSON-RPC 2.0 与之通信。
// 这一层取代了原来本地的 Tool.cpp —— 工具不再编译在一起，而是另一个进程提供。
#include "agent.h"
#include "json.hpp"
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>

using json = nlohmann::json;

// 进程间通信状态
static pid_t g_serverPid = -1; // MCP Server 子进程 PID
static int g_toServer = -1;    // 写端：Agent -> Server 的 stdin
static int g_fromServer = -1;  // 读端：Server 的 stdout -> Agent
static FILE* g_readFp = nullptr; // 包装读端，方便按行读取
static int g_nextId = 1;       // JSON-RPC 请求自增 id

// 发送一条 JSON-RPC 消息（写一行到 server 的 stdin）
static void sendMessage(const json& msg) {
    std::string line = msg.dump() + "\n";
    write(g_toServer, line.c_str(), line.size());
}

// 读取一行 JSON-RPC 响应并解析
static json readMessage() {
    char buf[8192];
    if (g_readFp && fgets(buf, sizeof(buf), g_readFp)) {
        return json::parse(std::string(buf), nullptr, false);
    }
    return json(nullptr);
}

// 发起一次「请求-响应」式调用（带 id），返回 result 字段
static json rpcCall(const std::string& method, const json& params) {
    json req;
    req["jsonrpc"] = "2.0";
    req["id"] = g_nextId++;
    req["method"] = method;
    if (!params.is_null()) req["params"] = params;
    sendMessage(req);

    json resp = readMessage();
    if (resp.is_discarded() || resp.is_null()) return json(nullptr);
    if (resp.contains("result")) return resp["result"];
    return json(nullptr);
}

// 拉起 MCP Server 子进程并完成 initialize 握手
bool mcpStart(const std::string& serverPath) {
    int inPipe[2];  // 父写 -> 子读（子进程的 stdin）
    int outPipe[2]; // 子写 -> 父读（子进程的 stdout）
    // 创建管道
    if (pipe(inPipe) < 0 || pipe(outPipe) < 0) return false;

    g_serverPid = fork();
    // fork 失败
    if (g_serverPid < 0) return false;
    // 子进程
    if (g_serverPid == 0) {
        // ===== 子进程：变身为 MCP Server =====
        dup2(inPipe[0], STDIN_FILENO);   // 子进程 stdin <- inPipe 读端
        dup2(outPipe[1], STDOUT_FILENO); // 子进程 stdout -> outPipe 写端
        close(inPipe[0]); close(inPipe[1]);
        close(outPipe[0]); close(outPipe[1]);
        execl(serverPath.c_str(), serverPath.c_str(), (char*)nullptr);
        // exec 失败才会走到这
        perror("execl mcp_server failed");
        _exit(127);
    }

    // ===== 父进程：保留通信用的管道端 =====
    close(inPipe[0]);   // 父进程不读 inPipe
    close(outPipe[1]);  // 父进程不写 outPipe
    g_toServer = inPipe[1];// 写端：Agent -> Server 的 stdin
    g_fromServer = outPipe[0];// 读端：Server 的 stdout -> Agent
    // 包装读端，方便按行读取
    g_readFp = fdopen(g_fromServer, "r");

    // initialize 握手
    json initParams = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", json::object()},
        {"clientInfo", {{"name", "mini-agent"}, {"version", "1.0.0"}}}
    };
    json initResult = rpcCall("initialize", initParams);
    if (initResult.is_null()) return false;

    // 发送 initialized 通知（无 id，不需回复）
    json notify;
    notify["jsonrpc"] = "2.0";
    notify["method"] = "notifications/initialized";
    sendMessage(notify);

    return true;
}

// tools/list：动态发现 Server 暴露的工具
std::vector<ToolSpec> mcpListTools() {
    std::vector<ToolSpec> specs;
    json result = rpcCall("tools/list", json(nullptr));
    if (result.is_null() || !result.contains("tools")) return specs;

    for (auto& t : result["tools"]) {
        ToolSpec spec;
        spec.name = t.value("name", "");
        spec.description = t.value("description", "");
        spec.inputSchema = t.contains("inputSchema") ? t["inputSchema"].dump() : "{}";
        specs.push_back(spec);
    }
    return specs;
}

// tools/call：请求 Server 执行某个工具，arguments 是 JSON 字符串
std::string mcpCallTool(const std::string& name, const std::string& arguments) {
    json args = json::parse(arguments, nullptr, false);
    if (args.is_discarded()) args = json::object();

    json params = {{"name", name}, {"arguments", args}};
    json result = rpcCall("tools/call", params);
    if (result.is_null()) return "Error: tool call failed";

    // 从 content 数组里把 text 拼出来
    std::string text;
    if (result.contains("content")) {
        for (auto& item : result["content"]) {
            if (item.value("type", "") == "text") text += item.value("text", "");
        }
    }
    return text.empty() ? "Error: empty tool result" : text;
}

// 关闭 MCP Server 子进程
void mcpStop() {
    if (g_toServer >= 0) { close(g_toServer); g_toServer = -1; }
    if (g_readFp) { fclose(g_readFp); g_readFp = nullptr; }
    if (g_serverPid > 0) {
        int status;
        waitpid(g_serverPid, &status, 0);
        g_serverPid = -1;
    }
}
