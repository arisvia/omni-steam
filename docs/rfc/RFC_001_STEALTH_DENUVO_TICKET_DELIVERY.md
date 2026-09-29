# RFC 001: 隐身化 Denuvo 票据传递方案 (Stealth Denuvo Ticket Delivery)

- **状态**: Proposed
- **作者**: OmniSteam Core Engineering
- **日期**: 2026-09-28
- **目标组件**: `libomnisteam` (Core), `omnisteam` (Manager), `OmniPlatform`

---

## 1. 背景与问题定义 (Problem Statement)

### 1.1 上游现状与痛点
上游项目（如 BetterSteamTools）针对 Denuvo 保护游戏采用了“**客户端全局命名管道 + 跨进程线程注入**”的实现方案：
1. **Steam 客户端内开放命名管道**：创建形如 `\\.\pipe\OpenSteamTool_<pid>` 的公开服务端管道。
2. **跨进程侵入游戏**：在 `SpawnProcess` 拦截点通过 `CreateRemoteThread` 或 DLL 注入将辅助模块注入目标游戏。
3. **管道握手取票**：游戏内注入模块向 Steam 客户端命名管道发起请求，获取并消费 Denuvo 激活票据。

### 1.2 安全与稳定性缺陷
- **重大检测面 (High Detection Vector)**：
  - 全局命名管道极易被现代反作弊系统（Easy Anti-Cheat、BattlEye、Ricochet、Denuvo Anti-Tamper 自身的文件/管道扫描器）全局枚举，被列入已知检测规则。
  - `CreateRemoteThread` / `WriteProcessMemory` 跨进程注入游戏直接触发各大杀毒软件与反作弊的内核钩子拦截，导致游戏闪退或账号封禁。
- **违背架构护城河**：
  - OmniSteam 的立项原则是 **“Headless 隐身注入”**（核心 Core 在 Steam 内部**零 Socket、零监听端口、零额外常驻线程**）。开放命名管道直接打破了这一安全护城河。

---

## 2. Denuvo 票据交互机制深度剖析 (Mechanism Analysis)

经对 Steam 客户端及 Denuvo 接入协议的逆向，Denuvo 游戏与 Steam 客户端的票据交互在协议层分为两类：

1. **客户端网络协议层 (Client-Side Network Path)**:
   - 游戏通过 `SteamUser()->RequestEncryptedAppTicket()` 请求加密票据。
   - 客户端组装 `k_EMsgClientRequestEncryptedAppTicket` (eMsg 5527) 发送至 Valve CM 服务器。
   - 客户端收到 CM 返回的响应后解码交还给游戏。
   - **此路径完全无需注入游戏进程**：只要在 Steam 客户端的 `RecvPkt` 处拦截 eMsg 5527/858，将本地缓存的 `ticket` 写入 protobuf 响应，游戏即可天然拿到合法票据。

2. **本地文件/离线激活层 (Local File / Offline Auth Path)**:
   - 很多 Denuvo 游戏的 Denuvo 模块会在本地存储设备绑定激活响应（例如 `dbdata` 目录、`%LOCALAPPDATA%\Denuvo\...` 或游戏同级目录）。
   - 一旦票据已在客户端或离线生成，直接在文件系统按规格放置对应文件，Denuvo 启动时会优先读取本地缓存，甚至不会发起 IPC 握手。

---

## 3. 隐身化架构设计方案 (Proposed Architecture)

OmniSteam 拟采用 **“双模零管道” (Dual-Mode Pipeless)** 隐身交付方案：

```
                    ┌─────────────────────────┐
                    │    Manager CLI / Web    │
                    │   (omnisteam import)    │
                    └────────────┬────────────┘
                                 │
                   落盘加密票据 (原子写盘, 用户权限 ACL)
                                 │
           ┌─────────────────────┴─────────────────────┐
           ▼                                           ▼
 模式 A: 协议层伪装 (主力)                     模式 B: 游戏侧零侵入代理 (独立游戏)
 ┌──────────────────────┐                     ┌──────────────────────┐
 │  libomnisteam.dll    │                     │  本地代理 DLL        │
 │  (在 Steam 客户端内) │                     │  (dxgi.dll/version)  │
 ├──────────────────────┤                     ├──────────────────────┤
 │ 拦截 eMsg 5527 / 858 │                     │ 读取用户本地票据库   │
 │ 追加 Ticket 字节流   │                     │ 本地模拟激活握手     │
 │ 零网络监听、零管道   │                     │ 零跨进程注入、无检测 │
 └──────────────────────┘                     └──────────────────────┘
```

### 3.1 模式 A：客户端纯协议层伪装 (Client-Side Wire Spoofing)
- **原理**：不触碰游戏进程，仅在 `libomnisteam.dll` 内增强现有的 `Hooks_NetPacket`。
- **实施细节**：
  1. Manager 导入票据时，统一写入 `%LOCALAPPDATA%\OmniSteam\credentials\<appid>\AppTicket`。
  2. `Hooks_NetPacket` 拦截 `k_EMsgClientRequestEncryptedAppTicketResponse` (5527) 与 `k_EMsgClientGetAppOwnershipTicketResponse` (858)。
  3. 当服务器返回 `eresult != k_EResultOK` 或未拥有时，自动读取本地票据二进制，通过手写的零拷贝 `ProtoFields` 追加 field 2 (`k_EResultOK`) 和 field 3 (`ticketBytes`)。
  4. 游戏通过标准的 Steamworks API 接收，完全符合官方交互逻辑，**反作弊无法从任何跨进程行为中发现异常**。

### 3.2 模式 B：游戏侧零侵入代理 (Drop-in Wrapper Proxy)
- **原理**：对脱离客户端直接拉起或有特殊本地绑定的游戏，采用“文件放置”而非“动态注入”。
- **实施细节**：
  1. 提供极简的只读包装器（如编译为 `version.dll` 或 `steam_api64.dll` 包装层）。
  2. 用户或 Manager 一键复制至游戏安装目录。
  3. 包装器启动时直接读取 `%LOCALAPPDATA%\OmniSteam\credentials\<appid>\AppTicket`，在本地响应 `GetAuthSessionTicket`。
  4. **严禁使用 `CreateRemoteThread` 或修改游戏二进制**，避免触发任何反作弊静态签名。

---

## 4. 安全防护与风险对照 (Security Matrix)

| 攻击面 / 检测项 | 上游方案 (BetterSteamTools) | OmniSteam RFC 001 方案 |
| :--- | :--- | :--- |
| **系统管道枚举** | 暴露 `\\.\pipe\OpenSteamTool_*` (高危) | **零管道、零命名内核对象** (完全隐身) |
| **跨进程注入** | `CreateRemoteThread` 注入游戏 (必被 EAC/BE 拦截) | **零跨进程内存操作** (无需注入游戏) |
| **文件权限隔离** | 管道默认 ACL 开放 (易被提权攻击) | 票据目录限定当前用户 SID (严格 ACL) |
| **反作弊误伤** | 所有游戏一视同仁挂钩 (竞技游戏高危) | `AntiCheatGuard` 竞技游戏白名单直通 |

---

## 5. 验收标准 (Acceptance Criteria)

1. Steam 进程在 Windows / Linux 下运行时，通过 `Process Hacker` / `handle.exe` 检测不到任何由 OmniSteam 创建的命名管道或 Socket 监听。
2. 导入合法 Denuvo 票据后，游戏在 Steam 客户端内拉起时能成功解析 `RequestEncryptedAppTicket` 并正常进入主界面。
3. 竞技类白名单游戏启动时，票据模块绝对不介入，原生直通。
