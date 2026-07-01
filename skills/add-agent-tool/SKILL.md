---
name: add-agent-tool
description: 为 main.cpp 的 ReAct agent 增加新工具的标准流程。当用户要求"加/接/挂 XX 工具"、"让 agent 能做 XX"、"扩展 agent 能力"时按此手册执行。
---

# Add Agent Tool

给 main.cpp 里的 ReAct agent 增加新工具的标准流程。

## Steps

1. **明确设计**:工具名(snake_case,动词开头)、参数、返回值示例。
2. **在 buildTools() 追加 ToolSpec**:name / description(给 LLM 看)/ inputSchema(JSON Schema 字符串)。
3. **在 executeTool() 加执行分支**:解析 args → 校验 → 执行 → 返回一句"人话"的 Observation。
4. **测试**:改 main() 里的 bot.run(...) 用一句自然语言触发新工具。
5. **验证 ReAct 循环**:应看到 `[Act] 工具名 {...}` → `[Observe] ...` → `[Final] ...`。

## Contract

- `ToolSpec.name` 必须与 `executeTool` 里的分支 name 完全一致
- 参数校验失败**不要 throw**,把错误当 Observation 返回,让模型自愈
- Observation 是"人话"不是 raw JSON

## Example: 加一个 get_current_time 工具

**buildTools() 追加**:
```cpp
ToolSpec t;
t.name = "get_current_time";
t.description = "获取当前系统时间(UTC),格式 YYYY-MM-DD HH:MM:SS";
t.inputSchema = R"({"type":"object","properties":{},"required":[]})";
tools.push_back(t);
```

**executeTool() 追加分支**:
```cpp
if (name == "get_current_time") {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
    return std::string("current time (UTC): ") + buf;
}
```

需要 `#include <chrono>` 和 `#include <ctime>`。
