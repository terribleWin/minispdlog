# 结构化日志

minispdlog 的结构化输出由 `json_formatter` 完成。专用 `json_*` Sink 把一条记录编码成一行 JSON，并以 `\n` 结束，适合按 JSON Lines / NDJSON 采集。

## 最小示例

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    minispdlog::json_formatter context;
    context.add("service", "checkout")
           .add("env", "prod")
           .add_int("instance", 3)
           .add_bool("sampled", true)
           .with_host();

    auto log = minispdlog::json_logger_mt(
        "orders", "logs/orders.jsonl", false, {}, context);
    log->info("accepted order {}", 1042);
    log->flush();
}
```

示意输出：

```json
{"time":"2026-10-10T08:30:00.123Z","ts":1791621000123,"level":"info","level_num":2,"logger":"orders","msg":"accepted order 1042","tid":123,"pid":456,"service":"checkout","env":"prod","instance":3,"sampled":true,"host":"node-a","source":{"file":"main.cpp","line":14,"func":"main"}}
```

具体时间、线程号、进程号和源码路径由运行环境决定。

## 固定字段

| 字段 | JSON 类型 | 含义 |
|------|-----------|------|
| `time` | string | UTC ISO-8601，毫秒精度，后缀 `Z` |
| `ts` | integer | Unix epoch 毫秒 |
| `level` | string | `trace` 到 `off` 的小写名称 |
| `level_num` | integer | 与 `minispdlog::level` 枚举数值一致 |
| `logger` | string | logger 名称 |
| `msg` | string | `fmt` 参数展开后的消息正文 |
| `tid` | integer | 记录创建线程的内部线程标识 |
| `pid` | integer | 进程标识 |
| `source` | object | 有源码位置时出现，包含 `file`、`line`、`func` |

`tid` 是库返回的进程内线程标识，不应跨进程当作全局 ID。源码位置由普通字符串字面量调用和 `MINISPDLOG_*` 宏捕获；手工构造没有位置的 `log_msg` 时不会输出 `source`。

## 资源字段

```cpp
minispdlog::json_formatter f;
f.add("region", "cn-east")
 .add_int("shard", 12)
 .add_bool("canary", false)
 .add_null("deployment")
 .with_host();
```

相同 key 再次添加会更新原字段。空 key 和下列保留 key 会被忽略：

`time`、`ts`、`level`、`level_num`、`logger`、`msg`、`tid`、`pid`、`source`。

资源字段属于 formatter 配置，不是每条日志的动态键值集合。应在开始并发记录前完成 `add*()`、`with_host()` 或 `clear_fields()`；不要在多个线程写日志时同时修改 formatter。

关联请求等动态信息可先放入 `msg`，或由业务封装一层固定字段协议。本版本没有 MDC、span context 或每调用动态 JSON 字段 API，不应在文档或接入层假设它们存在。

## 转义与编码

formatter 会转义引号、反斜杠、JSON 控制字符以及 Unicode 行分隔符 U+2028/U+2029，保证正常 UTF-8 输入中的换行不会破坏“一条记录一行”的边界。其他 UTF-8 字节原样保留。

库不会修复任意非法 UTF-8；若采集端要求严格 RFC 8259 JSON，调用方应保证 logger 名、payload、资源 key/value 和源码路径是有效 UTF-8。

## 专用 JSON Sink

| 场景 | 同步工厂 | 异步工厂 |
|------|----------|----------|
| 批量文件 + 恢复 | `json_logger_mt/st` | `async_json_file_mt` |
| 按大小滚动 | `rotating_json_logger_mt/st` | `async_json_rotating_mt` |
| 按日切分 | `daily_json_logger_mt/st` | 暂无快捷工厂 |
| stdout / stderr | `stdout_json_mt/st`、`stderr_json_mt/st` | 暂无快捷工厂 |
| Linux UDP / TCP | `json_udp_logger_mt/st`、`json_tcp_logger_mt/st` | `async_json_udp_mt`、`async_json_tcp_mt` |

`json_logger_mt` 使用 `buffered_file_sink`，因此默认存在批次窗口，并支持 WAL 与断尾修复；详情见[耐久性与恢复](durability-and-recovery.md)。

## JSON wrapper 的格式约束

专用 JSON Sink 默认锁定 `json_formatter`。对它调用 `set_pattern()` 或传入其他 formatter，不会把输出意外改回文本，而且已配置资源字段会被保留。

如果确实需要把 wrapper 当作底层 Sink 使用，可以显式解除约束：

```cpp
auto sink = std::make_shared<minispdlog::sinks::json_console_sink_mt>();
sink->allow_custom_formatting(true);
sink->set_pattern("[%L] %v");
```

解除后若当前 formatter 已不是 `json_formatter`，调用 `sink->json()` 会抛出 `std::logic_error`。通常更清楚的做法是直接选择对应的非 JSON Sink。

## 在任意 Sink 上安装 JSON formatter

```cpp
auto sink = std::make_shared<minispdlog::sinks::callback_sink_mt>(
    [](const std::string& line) {
        /// 将结构化日志投递到采集端。
        static_cast<void>(line);
    });
sink->set_formatter(std::make_unique<minispdlog::json_formatter>());
```

这适合自定义组合；专用 wrapper 则额外提供“保持 JSON”约束和 `json()` 配置入口。

## 采集建议

- 采集器按换行拆分，不要再把一行 JSON 包进文本 pattern。
- 用 `logger`、`service`、`env` 等低基数字段路由；把请求 ID 等高基数值放到明确字段或消息协议中。
- UDP 不保证到达，且单条记录不能超过配置的 `max_datagram`；重要链路选 TCP 或本地 durable 文件后由代理采集。
- schema 变化应视为接口变化。升级前用 JSON 解析测试验证字段类型，而不只比较字符串。

完整输出选择见 [Sink 指南](sinks.md)，字段 API 见 [API 速查](api.md)。
