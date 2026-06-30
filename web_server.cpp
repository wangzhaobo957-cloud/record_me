// web_server.cpp - 情感指导大师 Web 后端（C++ 版，无状态）
// 用 cpp-httplib（单头文件）做 HTTP 服务，直接复用现有的 callLLM（LLM.cpp）。
// 这才是"复用 C++ 现有结构"的做法：
//   - 网络层：cpp-httplib 负责监听端口、收 HTTP 请求、回 JSON
//   - 大模型层：完全复用 callLLM（同一个 Dashscope 端点、同一套 libcurl 调用）
//   - 情感大师人设：注入到 system prompt；不挂任何工具（tools 传空）
#include "agent.h"
#include "json.hpp"
#include "httplib.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>

using json = nlohmann::json;

// ===== 会话记忆 =====
// 每个访客一份独立的对话历史，按 session_id 隔离。
// 这就是"记忆"的本体：和你 C++ CLI 版 memory.cpp 里的 vector<Message> 思路一致，
// 只是这里用 map 给每个 session 各存一份。
static std::map<std::string, std::vector<Message>> g_sessions;
// cpp-httplib 是多线程的，多个请求可能同时读写 g_sessions，必须加锁。
static std::mutex g_sessionsMutex;

// 单个 session 最多保留多少条历史消息（不含 system）。
// 防止对话过长导致 token 爆掉——这就是最朴素的"上下文裁剪"。
static const size_t MAX_HISTORY = 20;

// 情感指导大师的人设（价值全在这段 prompt 里）
static const char* SYSTEM_PROMPT =
    "你是一位温暖、善解人意的情感指导大师，名叫\"暖心\"。\n"
    "你的使命是倾听、共情、并温柔地引导对方走出情绪困境。\n\n"
    "请遵循以下原则：\n"
    "1. 先共情，再建议：永远先接住对方的情绪，让对方感到被理解，再给出温柔的引导。\n"
    "2. 不评判、不说教：不要居高临下地讲道理，用平等、温暖的语气交流。\n"
    "3. 多倾听、多提问：通过温和的提问帮助对方梳理自己的感受和处境。\n"
    "4. 给出具体可行的小建议：在合适的时机，给出温柔、具体、可操作的小步骤。\n"
    "5. 语气温暖治愈：多用柔和、鼓励的措辞，让人感到安心。\n\n"
    "记住：你不是来解决问题的工具，而是一个愿意陪伴和倾听的朋友。";

// 读取本地文件内容（用于把 index.html 发给浏览器）
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    httplib::Server svr;

    // 首页：返回前端页面
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::string html = readFile("index.html");
        if (html.empty()) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
            return;
        }
        res.set_content(html, "text/html; charset=utf-8");
    });

    // 对话接口：有状态，按 session_id 记住整段对话
    svr.Post("/chat", [](const httplib::Request& req, httplib::Response& res) {
        json out;

        json in = json::parse(req.body, nullptr, false);
        if (in.is_discarded()) {
            out["reply"] = "请求格式不太对呢，再试一次好吗？";
            res.set_content(out.dump(), "application/json; charset=utf-8");
            return;
        }

        std::string userMessage = in.value("message", "");
        std::string sessionId = in.value("session_id", "");
        if (userMessage.empty()) {
            out["reply"] = "你想聊点什么呢？我在这里陪着你。";
            res.set_content(out.dump(), "application/json; charset=utf-8");
            return;
        }
        if (sessionId.empty()) sessionId = "default";

        // ===== 取出该 session 的历史，拼出本次要发给模型的完整消息 =====
        // 结构：system（人设） + 该 session 的历史（user/assistant 交替） + 本次 user 输入
        std::vector<Message> messages;
        {
            std::lock_guard<std::mutex> lock(g_sessionsMutex);
            std::vector<Message>& history = g_sessions[sessionId];

            // 先把用户这句话存进历史
            Message usr; usr.role = "user"; usr.content = userMessage;
            history.push_back(usr);

            // 裁剪：只保留最近 MAX_HISTORY 条，防止 token 无限膨胀
            if (history.size() > MAX_HISTORY) {
                history.erase(history.begin(), history.end() - MAX_HISTORY);
            }

            // 组装发给模型的消息：system 在最前，后面接整段历史
            Message sys; sys.role = "system"; sys.content = SYSTEM_PROMPT;
            messages.push_back(sys);
            for (auto& m : history) messages.push_back(m);
        }

        // 复用现有 callLLM；情感大师不需要工具，tools 传空
        std::vector<ToolSpec> noTools;
        Message reply = callLLM(messages, noTools);
        std::string replyText = reply.content.empty() ? "（暖心一时语塞了…）" : reply.content;

        // ===== 把模型回复也存回该 session 的历史，下一轮才能"记住" =====
        {
            std::lock_guard<std::mutex> lock(g_sessionsMutex);
            Message bot; bot.role = "assistant"; bot.content = replyText;
            g_sessions[sessionId].push_back(bot);
        }

        out["reply"] = replyText;
        res.set_content(out.dump(), "application/json; charset=utf-8");
    });

    const char* host = "127.0.0.1";
    int port = 5001;
    std::cout << "🌸 暖心情感指导大师已启动: http://" << host << ":" << port << std::endl;
    std::cout << "（请确保已设置环境变量 DASHSCOPE_API_KEY）" << std::endl;

    if (!svr.listen(host, port)) {
        std::cerr << "Error: 启动失败，端口 " << port << " 可能被占用" << std::endl;
        return 1;
    }
    return 0;
}
