// main.cpp - ReAct 雏形 + Skill 系统
// ReAct = Reason(思考) -> Act(行动/调工具) -> Observe(观察结果) 的循环。
// Skill  = 两阶段加载的"外挂 prompt"：
//   ① 启动时扫 ./skills/,只把 name+description 拼进 system prompt(轻量)
//   ② 模型决定要用某个 skill 时,调 load_skill 工具读全文(按需)
#include "agent.h"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ===== 一个真实可跑的工具：计算器（支持 + - * / 和括号）=====
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

// ===================== Skill 系统 =====================
// 一个 skill 的元数据（只存"目录信息",正文按需加载）
struct SkillMeta {
    std::string name;         // frontmatter: name
    std::string description;  // frontmatter: description
    std::string bodyPath;     // SKILL.md 的绝对路径,加载时才读
};

static std::vector<SkillMeta> g_skills;   // 全局 skill 清单(启动时扫一次)

// 读整个文件为字符串
static std::string readFileAll(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// 极简 frontmatter 解析：只认 --- ... --- 之间的 name: 和 description:
// 不引入 YAML 库,保持零依赖。仅支持 "key: 值"（值可以带引号,也可以不带）。
static bool parseFrontmatter(const std::string& content,
                             std::string& name,
                             std::string& description) {
    // 必须以 --- 开头
    if (content.rfind("---", 0) != 0) return false;
    size_t end = content.find("\n---", 3);
    if (end == std::string::npos) return false;

    std::string block = content.substr(3, end - 3);
    std::istringstream iss(block);
    std::string line;
    while (std::getline(iss, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        // trim
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
        };
        trim(k); trim(v);
        if (k == "name") name = v;
        else if (k == "description") description = v;
    }
    return !name.empty() && !description.empty();
}

// 扫描 ./skills/ 目录,加载每个 <skill>/SKILL.md 的 frontmatter
static std::vector<SkillMeta> loadSkillMetas(const std::string& dir) {
    std::vector<SkillMeta> out;
    if (!fs::exists(dir)) return out;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        auto md = entry.path() / "SKILL.md";
        if (!fs::exists(md)) continue;
        std::string content = readFileAll(md.string());
        SkillMeta m; m.bodyPath = md.string();
        if (parseFrontmatter(content, m.name, m.description)) {
            out.push_back(m);
            std::cout << "[Skill] loaded: " << m.name << " - " << m.description << "\n";
        }
    }
    return out;
}

// 把 skill 清单拼成一段说明,追加到 system prompt 后面
static std::string appendSkillsToPrompt(const std::string& basePrompt,
                                        const std::vector<SkillMeta>& skills) {
    if (skills.empty()) return basePrompt;
    std::string s = basePrompt + "\n\n## 可用 Skills（按需加载）\n";
    s += "遇到匹配场景时,调用 load_skill 工具读取对应 SKILL 的完整正文再执行。\n";
    for (const auto& m : skills)
        s += "- " + m.name + ": " + m.description + "\n";
    return s;
}

// ===================== 工具执行 =====================
// 执行某个工具调用,返回观察结果（Observation）字符串
static std::string executeTool(const std::string& name, const std::string& args) {
    if (name == "calculator") {
        json a = json::parse(args, nullptr, false);
        if (a.is_discarded() || !a.contains("expression"))
            return "工具参数错误：缺少 expression";
        std::string e = a["expression"].get<std::string>();
        return e + " = " + std::to_string(calc::eval(e));
    }
    // load_skill: skill 系统的"按需加载"入口 —— 本质就是一个特殊 tool
    if (name == "load_skill") {
        json a = json::parse(args, nullptr, false);
        if (a.is_discarded() || !a.contains("name"))
            return "工具参数错误：缺少 name";
        std::string want = a["name"].get<std::string>();
        for (const auto& m : g_skills) {
            if (m.name == want) {
                std::string body = readFileAll(m.bodyPath);
                if (body.empty()) return "Skill 文件为空或读取失败: " + want;
                // 返回整个 SKILL.md 正文,LLM 下一轮就能"看到"操作手册
                return "===== Skill [" + want + "] 已加载 =====\n" + body;
            }
        }
        return "未找到 skill: " + want;
    }
    return "未知工具: " + name;
}

// 声明可供模型"行动"的工具清单（动作空间）
static std::vector<ToolSpec> buildTools() {
    std::vector<ToolSpec> tools;

    ToolSpec calc;
    calc.name = "calculator";
    calc.description = "计算一个数学表达式,支持 + - * / 和括号";
    calc.inputSchema = R"({
        "type": "object",
        "properties": {
            "expression": {"type": "string", "description": "要计算的表达式,如 (3+5)*12"}
        },
        "required": ["expression"]
    })";
    tools.push_back(calc);

    // Skill 系统的核心工具:让 LLM 能"申请加载"某个 skill 的正文
    ToolSpec load;
    load.name = "load_skill";
    load.description = "按名称加载一个 skill 的完整操作手册。当系统提示里列出的某个 skill 匹配当前任务时调用。";
    load.inputSchema = R"({
        "type": "object",
        "properties": {
            "name": {"type": "string", "description": "要加载的 skill 名称,必须与清单里的 name 完全一致"}
        },
        "required": ["name"]
    })";
    tools.push_back(load);

    return tools;
}

// ===================== ReAct Agent =====================
class agent {
public:
    agent(std::string systemPrompt, std::vector<ToolSpec> tools)
        : tools_(std::move(tools)) {
        Message sys; sys.role = "system"; sys.content = std::move(systemPrompt);
        messages_.push_back(sys);
    }

    std::string run(const std::string& task, int maxSteps = 8) {
        Message usr; usr.role = "user"; usr.content = task;
        messages_.push_back(usr);

        for (int step = 0; step < maxSteps; ++step) {
            std::cout << "\n===== Step " << (step + 1) << " =====\n";
            Message reply = thought();
            messages_.push_back(reply);

            if (reply.toolCalls.empty()) {
                std::cout << "[Final] " << reply.content << "\n";
                return reply.content;
            }
            for (const auto& call : reply.toolCalls) {
                std::string observation = use(call);
                observe(call, observation);
            }
        }
        return "（已达最大步数,未收敛）";
    }

private:
    Message thought() { return callLLM(messages_, tools_); }

    std::string use(const ToolCall& call) {
        std::cout << "[Act]     " << call.name << " " << call.arguments << "\n";
        return executeTool(call.name, call.arguments);
    }

    void observe(const ToolCall& call, const std::string& observation) {
        // Observation 可能很长(整份 SKILL.md),打印时截断
        std::string preview = observation.size() > 200
            ? observation.substr(0, 200) + "...(截断,共 " + std::to_string(observation.size()) + " 字节)"
            : observation;
        std::cout << "[Observe] " << preview << "\n";
        Message m; m.role = "tool"; m.toolCallId = call.id; m.content = observation;
        messages_.push_back(m);
    }

    std::vector<Message> messages_;
    std::vector<ToolSpec> tools_;
};

// ===================== main =====================
int main() {
    // ① 启动时扫 skills 目录,只加载 metadata
    g_skills = loadSkillMetas("./skills");

    // ② 把 skill 清单拼进 system prompt(轻量,只是 name+description)
    std::string systemPrompt = appendSkillsToPrompt(
        "你是一个会使用工具和 skill 的智能体。需要数学计算时必须调用 calculator。"
        "当任务匹配某个 skill 时,先用 load_skill 加载它的正文,再按其指示执行。",
        g_skills
    );

    agent bot(systemPrompt, buildTools());

    // 测试用例:
    //   1) 普通计算 —— 直接用 calculator
    //   2) 匹配 skill —— 应该先 load_skill 再走 skill 指示的流程
    bot.run("帮我加一个查询当前时间的工具到 agent 里");
    return 0;
}
