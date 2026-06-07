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

using json = nlohmann::json;

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

    // 对话接口：无状态，每次只带 system + 当前用户输入
    svr.Post("/chat", [](const httplib::Request& req, httplib::Response& res) {
        json out;

        json in = json::parse(req.body, nullptr, false);
        if (in.is_discarded()) {
            out["reply"] = "请求格式不太对呢，再试一次好吗？";
            res.set_content(out.dump(), "application/json; charset=utf-8");
            return;
        }

        std::string userMessage = in.value("message", "");
        if (userMessage.empty()) {
            out["reply"] = "你想聊点什么呢？我在这里陪着你。";
            res.set_content(out.dump(), "application/json; charset=utf-8");
            return;
        }

        // 构造无状态消息历史：system（人设）+ user（本次输入）
        std::vector<Message> messages;
        Message sys; sys.role = "system"; sys.content = SYSTEM_PROMPT;
        Message usr; usr.role = "user";   usr.content = userMessage;
        messages.push_back(sys);
        messages.push_back(usr);

        // 复用现有 callLLM；情感大师不需要工具，tools 传空
        std::vector<ToolSpec> noTools;
        Message reply = callLLM(messages, noTools);

        out["reply"] = reply.content.empty() ? "（暖心一时语塞了…）" : reply.content;
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
