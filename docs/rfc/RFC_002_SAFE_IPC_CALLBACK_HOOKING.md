# RFC 002: 安全化 IPC 与回调拦截规范 (Safe IPC & Callback Hooking)

- **状态**: Proposed
- **作者**: OmniSteam Core Engineering
- **日期**: 2026-09-28
- **目标组件**: `libomnisteam` (`Hooks_CallBack`, `Hooks_IPC`)

---

## 1. 背景与问题定义 (Problem Statement)

### 1.1 上游现状
Steamworks 架构通过异步回调机制（Callback & CallResult）通知客户端 UI 和游戏当前状态（例如所有权变更 `LicensesUpdated_t`、网络就绪 `SteamServersConnected_t`、票据就绪 `EncryptedAppTicketResponse_t`）。
上游 BetterSteamTools 通过挂钩客户端内部的回调分发器（`CCallbackMgr::Dispatch` 或 `Steam_BGetCallback`），在原生回调流中动态注入、篡改或丢弃回调。

### 1.2 潜在风险与痛点
1. **死锁风险 (Re-entrancy Deadlock)**:
   - Steam 的回调分发系统在执行时持有全局回调锁。若在 Hook 内同步调用任何依赖该锁的 Steam 内部接口，将引发线程死锁。
2. **结构体 ABI 漂移 (ABI Drift)**:
   - 不同版本的 Steam SDK 与 steamclient 内部结构体可能存在对齐差异（例如 `#pragma pack(push, 8)` vs `pack(push, 4)`）。若强转错误，将导致栈破坏或内存非法访问。
3. **反作弊敏感度 (Anti-Cheat Sensitivity)**:
   - 部分具备自研反作弊的游戏会检查回调投递的时序和序列号。盲目伪造全局回调容易触发不一致性报警。

---

## 2. 设计原则 (Core Principles)

1. **C 导出函数优先于 C++ 内部虚表 (Export Over VTable)**:
   - 优先 Hook `Steam_BGetCallback` / `Steam_FreeLastCallback` 等稳定公开的 C API 边界，避免直接挂钩随时可能增减参数的 `CCallbackMgr` 内部成员函数。
2. **异步队列与非重入防护 (Isolated Async Dispatch)**:
   - 严禁在回调分发上下文中同步调用可能重入的 Hook 逻辑。合成回调必须通过线程安全队列在安静期投递。
3. **反作弊穿透与白名单门禁 (Anti-Cheat Short-Circuit)**:
   - 任何涉及 AppID 的回调（如统计、成就、票据），若命中 `AntiCheatGuard::IsProtected()`，必须直接放行原生逻辑，绝对禁止修改。
4. **编译期断言保护 (Compile-Time Layout Invariants)**:
   - 严格在 `SteamTypes.h` 中为关键回调 ID 建立 `static_assert`。

---

## 3. 详细架构设计 (Architecture & Implementation)

```
                    Steam 客户端 / 游戏调用 Steam_BGetCallback
                                     │
                                     ▼
                      ┌──────────────────────────────┐
                      │    hkSteam_BGetCallback      │
                      └──────────────┬───────────────┘
                                     │
                     是否有待消费的本地合成回调队列?
                                    / \
                              Yes  /   \ No
                                  /     \
             ┌───────────────────▼─┐   ┌─▼──────────────────┐
             │ 提取并消费合成事件  │   │  调用 oBGetCallback│
             │ (LicensesUpdated 等)│   └─────────┬──────────┘
             └─────────────────────┘             │
                                      原生回调返回
                                                 │
                                命中白名单游戏 or 未拦截 AppID?
                                                / \
                                          Yes  /   \ No
                                              /     \
                         ┌───────────────────▼─┐   ┌─▼─────────────────┐
                         │   透明直通原生返回  │   │ 字段按需改写/增强 │
                         └─────────────────────┘   └───────────────────┘
```

### 3.1 核心回调清单与安全策略

| 回调常量 | ID | 触发场景 | OmniSteam 安全处理策略 |
| :--- | :--- | :--- | :--- |
| `k_iCallback_LicensesUpdated` | 125 | 账户许可证列表更新 | 增量重载后仅在 Package 0 扩展完成时触发一次，通知 UI 刷新侧边栏；**零频率风暴防护** |
| `k_iCallback_EncryptedAppTicketResponse` | 154 | 加密票据响应就绪 | 仅针对已配置 Lua 的解锁 AppID，合成 `k_EResultOK`；非目标 App 原样透传 |
| `k_iCallback_SteamServersConnected` | 101 | 客户端连上 CM 服务器 | 监听以重置 PICS 令牌和签名缓存，**只读观察，不篡改** |
| `k_iCallback_UserStatsReceived` | 1101 | 用户成就与统计就绪 | 经 `AntiCheatGuard` 过滤，白名单游戏绝对禁止拦截 |

### 3.2 内存对齐与断言规范
```cpp
// 严格遵循 Steam SDK 跨平台对齐
#pragma pack(push, 8)
struct LicensesUpdated_t {
    enum { k_iCallback = 125 };
};
struct EncryptedAppTicketResponse_t {
    enum { k_iCallback = 154 };
    EResult m_eResult;
};
#pragma pack(pop)

static_assert(LicensesUpdated_t::k_iCallback == 125, "Callback ID drifted");
static_assert(EncryptedAppTicketResponse_t::k_iCallback == 154, "Callback ID drifted");
```

---

## 4. 落地步骤与风险控制 (Rollout & Risk Control)

1. **第一阶段 (只读监控)**：
   - 挂钩 `Steam_BGetCallback`，仅记录回调到达日志，验证在高并发（如启动下载、连续启动游戏）时无崩溃与死锁。
2. **第二阶段 (针对性合成)**：
   - 替换现有的 `MarkLicenseAsChanged`，改用标准的 `LicensesUpdated_t (125)` 回调队列分发，提升 SteamUI 侧边栏刷新速度。
3. **第三阶段 (白名单接入)**：
   - 将 `AntiCheatGuard` 接入回调层，确保竞技游戏全链路无干扰。
