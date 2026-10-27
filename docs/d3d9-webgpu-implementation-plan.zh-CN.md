# D3D9.0c → WebGPU 直接转译完整实施方案

> 前置阅读：`d3d9-webgpu-architecture.md`（架构总览，英文）与
> `d3d8-webgpu-architecture.md` / `d3d8-webgpu-implementation-plan.zh-CN.md`
> （已落地的 D3D8 路径，本方案在其之上复用协议范式、传输层与部分 host 基础设施）。
> 阅读本文前请先确认已经理解 D3D8 路径的 D8WG 协议设计和
> `glbridge/d3d8-webgpu/d3d8_executor.js` 的整体结构，本文不重复其中仍然适用
> 的结论,只标注 D3D9 特有的差异。

## 1. 背景与问题定义

D3D8→WebGPU 路径（D8WG 协议 v1.7）已经达到 M4+ 里程碑：完整定长管线、
vertex shader 1.1 / pixel shader 1.1-1.4 到 WGSL 的手写翻译器、贴图/曲面/
状态块/Reset 生命周期、设备丢失恢复、save state 集成，足以驱动 MapleStory
v0.83 这类以固定管线为主、SM1.x 为辅的游戏。

现在的目标是三款画面复杂度依次递增的 D3D9.0c 游戏：

- **跑跑卡丁车（KartRider）**：以固定管线为主，赛道/角色带少量 SM2.0 特效
  （水面、反光）。
- **魔兽争霸 3（Warcraft III，经典版，非重制版）**：固定管线为主，地形是
  多层贴图混合（terrain splatting），单位/特效使用少量与 SM1.x 接近的
  vertex/pixel shader。
- **魔兽世界（World of Warcraft）**：SM2.0/3.0 全面使用，顶点蒙皮
  （skinning）在 vertex shader 内完成，地形多层混合，动态多渲染目标
  （MRT）特效，遮挡查询（occlusion query），环境/立方体贴图，且内容随资料片
  持续变化，shader 种类事实上无上限。

D3D9 相对 D3D8，不是"多几个 opcode"的差异，而是有质的不同：

1. **真正的可编程着色器**。D3D8 的 shader model 1.x 字节码没有控制流，
   D3D8 路径能用一张 opcode 表手写翻译成 WGSL。D3D9 的 SM2.0/3.0 字节码有
   `if/else/endif`、`rep/endrep`、`loop/endloop`、`call/callnz/ret`、
   谓词指令（predicate）、相对寻址（`a0.x` 索引常量寄存器）、更大的寄存器
   堆和更大的 opcode 集合。这已经是"写一个小型编译器"的工程量，不是扩展
   一张表能解决的。
2. **顶点声明取代 FVF 作为主表示**。`IDirect3DVertexDeclaration9` 允许任意
   属性组合、任意多个 stream、非 `D3DCOLOR`/`FLOATn` 之外的紧凑格式
   （`UBYTE4N`、`SHORT2N`、`FLOAT16_2` 等），FVF 仅作为兼容路径保留。
3. **独立的 sampler state**。D3D8 的采样参数塞在 texture stage state 里，
   D3D9 把它们拆到 `SetSamplerState`，这与 WebGPU 的 `GPUSampler` 对象模型
   更接近，但意味着 D3D8 路径的 texture-stage 状态机不能直接套用。
4. **多渲染目标、独立查询对象、显式 scissor rect、更大的贴图格式矩阵**
   （立方体贴图、体积贴图、浮点格式、ATI1/ATI2 压缩法线贴图）。
5. **游戏本身的规模**。KartRider/War3 是有限的、可穷举的资源集合；WoW 是
   一个会不断补丁更新的活内容库，draw call 量级、shader 种类、贴图分辨率
   都远高于 MapleStory。

因此本方案不是"升级 D8WG"，而是设计一条**平行的 D3D9 路径**：独立协议
（D9WG）、独立 guest DLL（`d3d9.dll`）、独立 host executor
（`d3d9_executor.js`），在可复用的地方复用 D3D8 路径已经验证过的设计
（VGL2 传输、批处理时机、handle 分代、pipeline/bind-group 缓存、checkpoint
思路），但不假装两者是同一件事。

## 2. 目标、非目标与成功标准

### 2.1 项目目标

- 提供一条不经过 WineD3D/opengl32/gl4es 的 D3D9.0c → WebGPU 直接转译路径。
- 按游戏复杂度分里程碑交付：KartRider 可玩 → War3 单机战役可玩 → WoW 登录
  /选角可用 → WoW 主世界可玩 → WoW 特效/后处理与性能收敛。
- 复用现有 `v86gl.sys` / PCI BAR / 16 MiB DMA 环形区，不改动 D3D8 路径的
  外层 VGL2 ABI。
- 建立一条**已验证**的 SM2.0/3.0 字节码到 WGSL 编译管线，而不是"以后再说"。

### 2.2 非目标（本方案明确不做）

- 不实现 D3D9Ex（`IDirect3D9Ex`/`IDirect3DDevice9Ex`）。它依赖 WDDM 驱动
  模型和 Vista+ 语义，与 v86 虚拟的 Windows XP 客户机不匹配；三款目标游戏
  也都以 D3D9（非 Ex）为基线。
- 不实现 D3D10/D3D11。
- 不实现 DXVA 视频加速、跨进程共享 surface（`D3DPOOL_DEFAULT` +
  `IDirect3DSurface9` 共享句柄）。
- 第一阶段不实现运行时 HLSL 源码编译（`D3DXCompileShader`/
  `D3DXCompileShaderFromFile`）。只支持游戏在磁盘/资源包中携带的**已编译**
  字节码（`.fxo`/内嵌 DWORD 数组），这是绝大多数发行版游戏的常态；若阶段 0
  的 API trace 发现某游戏在运行时编译 HLSL 源码，单列为风险项并单独立项
  （见第 14 章）。
- 不追求任意 GPU 渲染结果的同步只读回读（`GetRenderTargetData` 对纯 GPU
  绘制内容）。延续 D3D8 路径的原则：CPU 影子拷贝对上传/Clear/CopyRects/
  StretchRect（已知源）精确，对"仅由任意 GPU draw 产生"的像素不做同步
  回读；如某游戏依赖这类回读，需要先设计异步 guest-stall 协议再解锁。
- 不提供、不分发任何游戏客户端文件或版权资源；本方案只覆盖用户自备正版
  客户端时的渲染兼容层。

### 2.3 分游戏成功标准

| 游戏 | 最低可玩标准 |
| --- | --- |
| KartRider | 主菜单、角色选择、至少一条赛道从起点跑到终点，帧率不低于 30fps，无渲染错误的地形/道具穿插 |
| Warcraft III | 单机战役任意一关可以从开始过场到任务目标达成，地形多层混合正确，单位选框/UI 正确 |
| WoW | 阶段性：先到登录+选角界面稳定运行；再到进入主世界后地形/角色/UI 正确渲染且可操作移动镜头，帧率与硬件基线相当（不要求媲美原生 D3D9 驱动） |

## 3. 总体架构

```text
game.exe（KartRider.exe / War3.exe / Wow.exe）
  -> app-local d3d9.dll（COM + 顶点声明/着色器字节码影子 + 批处理）
  -> VGL2 DMA 传输信封 / D9WG 高层命令流（VGL2 fn = 0xFFE1）
  -> d3d9_executor.js（资源/状态表 + 字节码翻译管线 + pipeline 缓存）
  -> WebGPU / WGSL
```

D3D8 路径保持不变：

```text
MapleStory.exe -> d3d8.dll -> VGL2 fn=0xFFE0 / D8WG -> d3d8_executor.js -> WebGPU
```

两条路径共享 `v86gl.sys`、16 MiB DMA 环形区、PCI BAR 与
`v86gl-pci-frame` 事件，不共享渲染后端，也不共享 guest DLL：一个进程只加载
`d3d8.dll` 或 `d3d9.dll` 中的一个（见第 20 章部署互斥规则）。

### 3.1 与 D3D8 路径的复用边界

**直接复用设计范式，但各自实现一份：**

- VGL2 信封内嵌一个自描述批次（batch header + 一串带 size 的 command）。
- 资源 handle 采用"分代"编码，防止跨 Reset/跨会话的悬空句柄复用。
- guest 端持有真相状态（shadow state），host 端只持有 GPU 真相，不猜测
  guest 意图。
- 一帧内合批，`Present` 或 DMA 容量压力触发提交。
- pipeline/bind-group/sampler 均为有界 LRU 缓存，key 由不可变状态派生。

**建议抽成共享模块，供两个 executor 一起 import（新增
`glbridge/webgpu-runtime/` 目录）：**

- WebGPU adapter/device 获取、丢失监听与重建流程。
- overlay canvas 生命周期（与 gl4es canvas 并存但独立 context 的规则）。
- 通用有界 LRU 缓存实现（当前 `d3d8_executor.js` 内联实现的那部分）。
- checkpoint 文件头（magic/version）读写辅助——两条协议的 checkpoint 内容
  不同，但"版本化二进制 checkpoint"这件事的样板代码相同。

**不共享：** COM vtable 定义、opcode 枚举、shader 字节码解析/翻译、
资源表结构体形状。D3D8 与 D3D9 的资源/状态模型差异大到共享会让两边都变得
难以独立演进。

### 3.2 组件职责边界

| 组件 | 职责 |
| --- | --- |
| `glbridge/d3d9proxy/d3d9.dll` | COM 对象、顶点声明/FVF 影子、着色器字节码影子（原始 token 流 + hash）、状态去重、批处理、DMA 提交 |
| `v86gl.sys` / v86 PCI 设备 | 与 D3D8 路径完全一致，不感知 D9WG 内容，只做一次同步 guest-RAM 读 |
| `glbridge/d3d9-webgpu/d3d9_executor.js` | D9WG 解码、资源/状态表、字节码编译管线调用、pipeline/bind-group 缓存、WebGPU 编码与提交 |
| `glbridge/webgpu-runtime/`（新增） | D3D8/D3D9 共享的 WebGPU 设备生命周期与通用缓存基础设施 |

### 3.3 外部参考实现评估：`LostMyCode/d3d9-webgl`

社区里已有一个公开的 D3D9→Web 实现可以对照：
[`LostMyCode/d3d9-webgl`](https://github.com/LostMyCode/d3d9-webgl)（MIT
许可，用于将 *GunZ: The Duel*（2003）移植到浏览器，即
[Whiplash GunZ](https://gunz.sigr.io/)）。评估结论：**架构模型不同，
不能直接复用，但有三处具体技术值得借鉴**（见 4.7、8.4、9.11）。

不能直接复用的原因：

- **集成方式不同**。它是"drop-in 头文件 + 一个 `.cpp`"，游戏**源码**
  直接 `#include "d3d9.h"` 并用 Emscripten 与游戏逻辑一起编译成同一个
  WASM 二进制，DirectX 调用在编译期就变成直接的 WebGL 调用——这要求拿到
  游戏源码重新编译。本方案的前提正相反：目标是三款**不可获得源码、不
  可重新编译**的商业游戏,运行在 v86 模拟的未修改 Windows XP 客户机里,
  通过 drop-in 的 `d3d9.dll` + PCI DMA + 独立浏览器进程通信,这是二进制
  级兼容层,两种集成模型没有共享代码的空间。
- **渲染后端不同**。它的目标是 WebGL 2.0 / OpenGL ES 3.0,本方案的目标
  是 WebGPU / WGSL,状态模型（尤其是 pipeline/bind group 与 GLSL 全局
  状态机的差异）不通用。
- **它明确不解决本方案最难的问题**。README 里写得很直接：
  `GetDeviceCaps` 故意报告 `VertexShaderVersion = 0`，把应用"骗"进自己
  的固定管线代码路径,项目本身只实现了一个 vertex + 一个 fragment shader
  覆盖全部 FFP 状态组合,完全没有 SM2.0/3.0 字节码翻译。这恰好是第 9 章
  要解决的核心难点,该项目对此没有参考价值。
- **功能覆盖明显更窄**：单 vertex stream（不支持多流顶点声明）、最多
  3 个点光源硬编码、仅线性表雾（EXP/EXP2/range fog 被接受但无效果）、
  渲染目标不支持 `LockRect` 回读。这些限制对 KartRider/War3 可能可以
  接受,但 WoW 的顶点蒙皮天然需要多 stream（`BLENDWEIGHT`/
  `BLENDINDICES` 在独立 stream 里很常见）,不能套用这套限制。

## 4. 核心架构决策

### 4.1 D9WG 是独立协议，复用 framing 规则

理由已在第 1、3 章说明。具体收益：D9WG 的 command 编号、resource kind
编号可以从 1 重新开始，不需要与 D8WG 的编号空间对齐或避让,两边各自的
opcode 表可以独立增删而不必担心跨协议冲突。

### 4.2 字节码编译放在"首次遇到该 shader 时"，而不是每帧

guest 端只做三件事：解析字节码 token 数量（用于确定 payload 边界，方式与
D8WG 的 `instruction_token_count` 完全一致）、计算内容 hash、把原始 token
流连同 hash 一起放进 `D9WG_OP_CREATE_VERTEX_SHADER`/`CREATE_PIXEL_SHADER`
payload。guest **不**做任何语义翻译。

host 端收到 CREATE 命令后：

1. 用 hash 查编译结果缓存（内存 + 可选持久化，见第 19 章）。
2. 缓存命中：直接复用已缓存的 WGSL 源码与 `GPUShaderModule`。
3. 缓存未命中：跑一次 vkd3d-shader → SPIR-V → Tint → WGSL → 
   `device.createShaderModule` + `getCompilationInfo()` 校验，成功后写入
   缓存；失败则该 shader handle 标记为"不可用"，绑定它的 draw 调用直接跳过
   （不能让编译失败导致整批命令读取错位或崩溃）。

这一步必须在 `CREATE_*_SHADER` 命令处理时做完，不能推迟到第一次 `Draw`，
否则第一次使用某 shader 的那一帧会有不可预测的延迟尖峰且难以定位。

### 4.3 顶点声明是主表示，FVF 是兼容视图

`D8WG`（D3D8）里 FVF 就是唯一表示。D3D9 里两者并存：guest 端把
`SetFVF()` 内部转换成一个等价的 `D3DVERTEXELEMENT9` 数组（复用 D3D8 路径
已经写过的 FVF→属性布局映射代码，逻辑不变，只是产物形状变了），host 端
**只认** vertex declaration 描述，不单独实现第二套"FVF 直读"逻辑。这样
input-layout 推导只有一条代码路径。

### 4.4 独立 sampler state 是简化，不是负担

D3D9 把采样参数从 texture stage state 分离出来，恰好更贴近
`GPUSampler`：一个 D9WG `SetSamplerState` 序列可以直接映射成一次
sampler-cache 查找/创建，不需要像 D3D8 路径那样从纠缠在一起的 texture
stage 状态里拆解采样参数。texture stage state 在 D3D9 里保留的部分（颜色/
alpha 运算、参数来源、`D3DTOP_*`）仅用于**没有绑定 pixel shader 的固定
管线路径**，与 D3D8 的处理方式相同。

### 4.5 guest 保真状态、host 保真 GPU：原则延续

与 D3D8 路径一致：Reset/设备丢失后，guest 端的影子状态是唯一真相来源，
host 端资源表可以整体重建；host 端不缓存任何"猜测的" guest 意图。

### 4.6 部署 profile 继续互斥

一个游戏目录只能有 `d3d8.dll`、`d3d9.dll`、自定义 `opengl32.dll`
（WineD3D 回退）三者之一。三条路径都要往同一个 16 MiB DMA 环形区写批次，
混用会导致命令流交织、v86 PCI 侧无法区分归属。

### 4.7 WoW 存在真实的历史级 FFP 回退路径，可以用来拆出一个降风险的中间里程碑

`d3d9-webgl` 靠"把 caps 报告成零 shader 支持，让游戏自己走固定管线代码
路径"这一招就撑起了 GunZ 的移植，前提是 GunZ 本身确实带有可用的 FFP
代码路径。这不是巧合：**魔兽世界发布时（2004 年）的官方最低配置是
GeForce 2 MX / Radeon 7000 一类完全没有可编程 shader 的显卡**，这意味着
Blizzard 的引擎里确实存在一条经过发布前测试、真实可玩的固定管线渲染
路径（对应游戏内画质选项里最低的"地形/固定功能"档位），而不是需要我们
自己拼凑近似效果。

这给第 15 章的里程碑规划提供了一个值得采纳的调整：在 M4（WoW 登录/
选角）和 M5（WoW 主世界，要求完整 SM2.0/3.0 管线）之间，插入一个
**M4.5：WoW 固定管线/最低画质路径**——`d3d9.dll` 的 `GetDeviceCaps`
按照 M1-M4 已完成的固定管线能力诚实上报（`VertexShaderVersion`/
`PixelShaderVersion` 报 0 或报很低的值,不是"骗"，是如实反映当前尚未
实现 SM2.0/3.0），让 WoW 客户端按自己的画质自适应逻辑选中它自带的低
配置渲染路径。这样可以在**不等待第 9 章整条字节码编译管线跑通**的
情况下，提前验证 WoW 的地形/角色/UI 在本方案的固定管线基础设施上能不能
正确显示，把"WoW 到底能不能跑起来"这个大问题拆成"低画质能跑"和"高画质
需要完整 SM2.0/3.0"两个独立可验证的问题。

需要注意的边界：这一招对 GunZ 有效是因为 GunZ 允许运行时把
`VertexShaderVersion` 探测为 0 并优雅退回；阶段 0 的 WoW API trace
（第 5 章）需要验证客户端在检测到低 shader 版本时是否真的有一条完整
可用的降级路径而不是直接报错拒绝启动或画面大量缺失（例如角色皮肤在纯
FFP 路径下是否还能贴纹理、地形多层混合在没有 pixel shader 时是否退化为
可接受的单层贴图）——如果实测发现降级路径本身残缺，M4.5 就不成立，
需要直接进入完整 SM2.0/3.0 路径。

## 5. 阶段 0：三款游戏各自建立基线与 API 面追踪

在写任何 D9WG 代码之前，必须先量化"要支持多少"，否则第 9 章的字节码编译
管线和第 11 章的格式矩阵范围都无法收敛。

对三款游戏分别做：

1. **固定测试环境**：与 D3D8 路径相同的方法——在真实 Windows XP（非 v86）
   下用 WineD3D/系统自带 d3d9.dll 跑，用 API 拦截工具（例如自制的 LD_
   PRELOAD 风格 `d3d9.dll` 转发代理 + 日志，或者 apitrace 的 D3D9 支持）
   录制真实调用序列。
2. **旧链路指标基线**：帧率、draw call 数、状态切换数、贴图创建峰值。
3. **API 使用面统计**：出现过的 `D3DRS_*`/`D3DSAMP_*`/`D3DTSS_*`、贴图
   格式集合、shader 版本分布（`vs_1_1`/`vs_2_0`/`vs_3_0`/`ps_2_0`/
   `ps_3_0` 各自的调用/绘制占比）、是否调用了 D3DX9 入口、是否使用
   `SetStreamSourceFreq`（instancing）、是否使用超过 4 个同时纹理阶段、
   是否使用非默认 swap chain。
4. **贴图/顶点声明枚举**：收集实际出现过的 `D3DVERTEXELEMENT9` 组合与
   贴图格式集合，作为第 10、11 章矩阵的输入而不是凭空列举 D3D9 规格书
   里的全部可能值。

### 阶段 0 退出条件

- 三款游戏各自有一份可复现的 API trace 和调用面统计表。
- 明确回答："是否运行时编译 HLSL 源码"、"是否用到体积贴图/立方体贴图"、
  "是否用到 MRT"、"是否用到遮挡查询"、"D3DX9 具体链接了哪些入口"。
- D9WG v0.1 协议草案（第 6 章）根据统计结果冻结第一批 opcode 集合。

## 6. D9WG 二进制协议设计

### 6.1 基本规则（与 D8WG 相同）

- little-endian，`#pragma pack(push, 1)`，所有跨语言共享结构体在协议头里
  用静态断言锁定 `sizeof`。
- 一次 VGL2 `0xFFE1` 提交承载一个完整的、自描述的 D9WG batch。
- 每条 command 都带 `size`，parser 可以在不理解 opcode 语义的情况下跳过
  未知/不支持的 command（用于版本前向兼容的降级处理，但**不能**用于假装
  执行了不支持的操作——不支持的 opcode 仍然要让对应 D3D9 API 调用返回
  `D3DERR_INVALIDCALL`，"跳过"只是 parser 的容错手段，不是功能实现）。

### 6.2 batch header

```c
#define V86GL_CTRL_D3D9_BATCH 0xFFE1u

#define D9WG_MAGIC 0x47573944u /* "D9WG" */
#define D9WG_VERSION_MAJOR 1u
#define D9WG_VERSION_MINOR 0u

#pragma pack(push, 1)
typedef struct D9WGBatchHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t frame_id;
    uint32_t flags;
    uint32_t command_count;
    uint32_t command_bytes;
    uint32_t session_id_low;
    uint32_t session_id_high;
} D9WGBatchHeader; /* 32 bytes, 与 D8WGBatchHeader 形状一致 */

typedef struct D9WGCommandHeader {
    uint16_t opcode;
    uint16_t flags;
    uint32_t size;
    uint32_t sequence;
    uint32_t reserved;
} D9WGCommandHeader; /* 16 bytes */
#pragma pack(pop)
```

### 6.3 版本与 feature negotiation

与 D8WG 相同：guest 在 `D9WG_OP_HELLO` 中携带 `feature_bits`，host 拒绝
不认识的 major 版本、拒绝比自己新的 minor 版本（宁可拒绝启动也不要静默
丢弃功能）。`session_id_low/high` 复用 D8WG 已验证过的"64 位每进程会话
命名空间"设计，防止跨会话数字 handle 碰撞。

### 6.4 资源 handle 与命名空间

D9WG 的资源比 D8WG 多两类（顶点声明、查询对象），handle 高位仍用于区分
kind，具体分配：

```c
#define D9WG_RESOURCE_BUFFER_VERTEX      1u
#define D9WG_RESOURCE_BUFFER_INDEX       2u
#define D9WG_RESOURCE_TEXTURE_2D         3u
#define D9WG_RESOURCE_TEXTURE_CUBE       4u
#define D9WG_RESOURCE_TEXTURE_VOLUME     5u
#define D9WG_RESOURCE_VERTEX_DECLARATION 6u
#define D9WG_RESOURCE_VERTEX_SHADER      7u
#define D9WG_RESOURCE_PIXEL_SHADER       8u
#define D9WG_RESOURCE_QUERY              9u
#define D9WG_RESOURCE_STATE_BLOCK        10u
```

着色器 handle 继续沿用 D8WG 的约定：bit 0 恒为 1，与 FVF token（bit 0
恒为 0，因为合法 FVF 值的 bit0 对应 `D3DFVF_RESERVED0`,规范禁止使用）
区分开，这样 `SetVertexShader`/旧式 `SetFVF` 兼容路径可以共用一个字段而
不产生歧义（延续 D8WG 已验证的技巧）。

### 6.5 opcode 分组（v0.1 草案，阶段 0 结果会调整具体子集）

```c
enum D9WGOpcode {
    D9WG_OP_HELLO = 1,
    D9WG_OP_CREATE_DEVICE = 2,
    D9WG_OP_RESET = 3,
    D9WG_OP_PRESENT = 4,
    D9WG_OP_CLEAR = 5,
    D9WG_OP_BEGIN_SCENE = 6,
    D9WG_OP_END_SCENE = 7,
    D9WG_OP_STRETCH_RECT = 8,        /* 新：D3D9 一等 API */
    D9WG_OP_COLOR_FILL = 9,          /* 新：D3D9 一等 API */
    D9WG_OP_UPDATE_SURFACE = 10,

    D9WG_OP_CREATE_BUFFER = 0x100,
    D9WG_OP_UPDATE_BUFFER = 0x101,
    D9WG_OP_DESTROY_RESOURCE = 0x103,
    D9WG_OP_CREATE_TEXTURE_2D = 0x110,
    D9WG_OP_CREATE_TEXTURE_CUBE = 0x111,     /* 新 */
    D9WG_OP_CREATE_TEXTURE_VOLUME = 0x112,   /* 新 */
    D9WG_OP_UPDATE_TEXTURE = 0x113,
    D9WG_OP_CREATE_VERTEX_DECLARATION = 0x120, /* 新 */
    D9WG_OP_CREATE_VERTEX_SHADER = 0x121,
    D9WG_OP_CREATE_PIXEL_SHADER = 0x122,
    D9WG_OP_CREATE_QUERY = 0x123,            /* 新 */
    D9WG_OP_CREATE_STATE_BLOCK = 0x124,

    D9WG_OP_SET_RENDER_STATE = 0x200,
    D9WG_OP_SET_SAMPLER_STATE = 0x201,       /* 新：独立于 texture stage */
    D9WG_OP_SET_TEXTURE_STAGE_STATE = 0x202, /* 仅剩色彩/alpha 运算部分 */
    D9WG_OP_SET_TEXTURE = 0x203,
    D9WG_OP_SET_VIEWPORT = 0x204,
    D9WG_OP_SET_SCISSOR_RECT = 0x205,        /* 新：D3D9 一等 API */
    D9WG_OP_SET_TRANSFORM = 0x206,
    D9WG_OP_SET_MATERIAL = 0x207,
    D9WG_OP_SET_LIGHT = 0x208,
    D9WG_OP_LIGHT_ENABLE = 0x209,
    D9WG_OP_SET_STREAM_SOURCE = 0x20A,
    D9WG_OP_SET_STREAM_SOURCE_FREQ = 0x20B,  /* 新：instancing，M6 前不实现 */
    D9WG_OP_SET_INDICES = 0x20C,
    D9WG_OP_SET_VERTEX_DECLARATION = 0x20D,
    D9WG_OP_SET_FVF = 0x20E,                 /* 兼容路径 */
    D9WG_OP_SET_RENDER_TARGET = 0x20F,       /* 带 target index，支持 MRT */
    D9WG_OP_SET_DEPTH_STENCIL_SURFACE = 0x210,
    D9WG_OP_SET_VERTEX_SHADER = 0x211,
    D9WG_OP_SET_PIXEL_SHADER = 0x212,
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F = 0x213,
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_I = 0x214,
    D9WG_OP_SET_VERTEX_SHADER_CONSTANT_B = 0x215,
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F = 0x216,
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_I = 0x217,
    D9WG_OP_SET_PIXEL_SHADER_CONSTANT_B = 0x218,
    D9WG_OP_SET_CLIP_PLANE = 0x219,          /* 新，见 9.11 */

    D9WG_OP_DRAW_PRIMITIVE = 0x300,
    D9WG_OP_DRAW_INDEXED_PRIMITIVE = 0x301,
    D9WG_OP_DRAW_PRIMITIVE_UP = 0x302,
    D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP = 0x303,

    D9WG_OP_BEGIN_QUERY = 0x400,             /* 新 */
    D9WG_OP_END_QUERY = 0x401,               /* 新 */
    D9WG_OP_GET_QUERY_DATA = 0x402           /* 新，见 6.7 同步语义 */
};
```

关键 payload 结构体草案（与 D8WG 的写法一致，字段顺序按访问频率排列，
这里只列出 D8WG 里没有对应物的新结构体）：

```c
/* D3DVERTEXELEMENT9 数组的线性编码：紧跟在 header 之后是
 * element_count 个 8 字节 D9WGVertexElement，不含 D3DDECL_END 哨兵，
 * 与 D8WG 的 shader token 计数约定一致（host 从 count 推导边界）。 */
typedef struct D9WGVertexElement {
    uint16_t stream;
    uint16_t offset;
    uint8_t  type;      /* D3DDECLTYPE */
    uint8_t  method;     /* D3DDECLMETHOD, 非 DEFAULT 直接拒绝 */
    uint8_t  usage;      /* D3DDECLUSAGE */
    uint8_t  usage_index;
} D9WGVertexElement; /* 8 bytes */

typedef struct D9WGCreateVertexDeclaration {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t element_count;
    uint32_t reserved;
    /* element_count 个 D9WGVertexElement 紧随其后 */
} D9WGCreateVertexDeclaration;

/* CREATE_VERTEX_SHADER / CREATE_PIXEL_SHADER 都新增 bytecode_hash，
 * 供 host 做首次编译缓存查找；其余字段与 D8WG 的等价结构体同构。 */
typedef struct D9WGCreateVertexShader {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t instruction_token_count;
    uint32_t code_offset;
    uint32_t bytecode_hash_low;
    uint32_t bytecode_hash_high;
} D9WGCreateVertexShader;
/* D9WGCreatePixelShader 同构 */

typedef struct D9WGSetShaderConstantF {
    uint32_t device_handle;
    uint32_t start_register;
    uint32_t vector_count;   /* float4 个数 */
    uint32_t data_offset;    /* 相对 batch 基址，vector_count*16 字节 */
} D9WGSetShaderConstantF;
/* SetShaderConstantI 类似，但 data_offset 指向 int4；
 * SetShaderConstantB 的 data_offset 指向按 32 位对齐打包的 bool 数组。 */

typedef struct D9WGSetRenderTarget {
    uint32_t device_handle;
    uint32_t target_index;         /* 0..3，MRT */
    uint32_t color_texture_handle; /* 0 = 解除绑定该槽位 */
    uint32_t color_level;
} D9WGSetRenderTarget;

typedef struct D9WGSetScissorRect {
    uint32_t device_handle;
    int32_t  left;
    int32_t  top;
    int32_t  right;
    int32_t  bottom;
} D9WGSetScissorRect;

typedef struct D9WGCreateQuery {
    uint32_t device_handle;
    uint32_t resource_handle;
    uint32_t query_type; /* D3DQUERYTYPE_OCCLUSION / EVENT，第一版仅这两种 */
    uint32_t reserved;
} D9WGCreateQuery;
```

M1 落地时发现的一处需要修正：上面"与D8WG同构"的假设对 `SetIndices`/
`DrawIndexedPrimitive` 不成立。`IDirect3DDevice9::SetIndices` 的签名只有
`(IDirect3DIndexBuffer9*)`，不带 base vertex index——D3D9 把这个参数移到了
`DrawIndexedPrimitive` 本身（`BaseVertexIndex` 是逐次绘制传入的，不是设备
状态）。因此实际实现里 `D9WGSetIndices` 只有 `device_handle`/
`buffer_handle` 两个字段，`D9WGDrawIndexedPrimitive` 比 D8WG 版本多一个
`int32_t base_vertex_index` 字段。另外 4.3 节"host 端只认 vertex
declaration 描述"的原则落到协议上意味着 `D9WG_OP_SET_FVF` 的 payload
不能只带原始 FVF DWORD——那样 host 还是要自己解码 FVF，等于两套布局
逻辑。实际形状是 guest 侧用与 `CREATE_VERTEX_DECLARATION` 相同的
`D9WGVertexElement` 数组把 FVF 展开后随命令发送，`fvf` 字段只保留给
host 端日志/诊断用，不参与解码。具体形状见
`glbridge/d3d9proxy/d3d9_protocol.h`（已按此修正实现并冻结）。

### 6.6 上传区

延续 D8WG 的共享 16 MiB transient upload ring 设计（`Draw*UP`、动态
buffer/texture staging 共用一段环形区,按 D8WG_ALIGN8 对齐）。D3D9 新增的
体积贴图更新（`LockBox`）沿用同一环形区，只是 `UpdateTexture` payload
多了一个 `depth`/`slice_pitch` 字段。

### 6.7 同步命令与查询语义

`GET_QUERY_DATA`（对应 `IDirect3DQuery9::GetData`）是本协议第一个**必须
往返**的命令：D3D9 允许 `GetData(NULL, 0, 0)` 做非阻塞轮询，也允许
`D3DGETDATA_FLUSH` 阻塞等待。第一版只支持轮询语义：guest 侧
`GetData` 在结果未就绪时直接返回 `S_FALSE`（与真实驱动允许的行为一致），
不阻塞、不做同步 PCI 往返；host 端在每次 `Present` 之后，把已完成的
occlusion query 结果写入一小块"query 结果影子区"（复用 checkpoint 里
已有的、host→guest 单向状态回传机制的思路，若该机制在 D3D8 路径中尚不
存在则在本阶段新增，只用于极小数据量的查询结果，不用于纹理回读）。

### 6.8 parser 安全要求

与 D8WG 相同：越界读、非法 command_count/size、未知 major 版本一律拒绝
整个 batch；`D9WGVertexElement`/shader token 数组长度必须先做算术溢出
检查再乘以元素大小，这一步在 D8WG 的实现中已经踩过坑，D9 的 parser 要
从一开始就写对。

## 7. Windows guest：自定义 `d3d9.dll`

### 7.1 建议目录结构

```text
glbridge/d3d9proxy/
  d3d9.def
  d3d9_protocol.h        # 对应本文件第 6 章，与 host 端共享的二进制契约
  d3d9_proxy.c            # 单文件实现（延续 d3d8_proxy.c 的组织方式）
  build.sh
  build_stageN_tests.sh   # 按里程碑拆分的验收测试构建脚本
  README.md
```

### 7.2 与 d3d8proxy 的代码复用策略

**不共享同一个 `.c` 文件，但共享一层传输基础设施头文件**：把 D8WG 路径
里"与 D3D8 语义无关"的部分——VGL2 DMA 提交、批次 flush 时机判断、DMA
环形区分配器、command header 写入辅助——抽成
`glbridge/d3d9proxy/../vgl2_transport.h`（新增，纯头文件，无外部依赖，
不依赖 CRT），`d3d8_proxy.c` 后续可以选择性迁移过去，但**不要求**本次
改造同步重构 D3D8 路径；新代码先在 `d3d9_proxy.c` 里独立实现一份，等
D3D9 路径跑通到 M2 里程碑、传输层设计稳定之后，再回头把 D8 路径迁移到
共享头文件（避免两条路径同时处于"传输层正在改"的不稳定状态）。

COM vtable、状态影子结构体、shader token 影子、顶点声明影子等 D3D9 特有
逻辑完全独立实现，不与 `d3d8_proxy.c` 共享。

### 7.3 COM 对象实现顺序

1. `IDirect3D9`：`CreateDevice`、适配器/格式/多重采样探测、`GetDeviceCaps`
   （caps 必须诚实反映当前里程碑已实现的功能，未实现能力不得声明支持，
   延续 D3D8 路径的"caps 审计"纪律）。
2. `IDirect3DDevice9`：`Clear`/`BeginScene`/`EndScene`/`Present`、
   `SetRenderState`/`SetSamplerState`/`SetTextureStageState`。
3. `IDirect3DVertexDeclaration9`：`CreateVertexDeclaration`/
   `SetVertexDeclaration`，FVF→声明的内部转换复用 D3D8 路径已验证的映射
   表逻辑。
4. `IDirect3DVertexBuffer9`/`IDirect3DIndexBuffer9`：`Lock`/`Unlock`，
   `D3DLOCK_DISCARD`/`D3DLOCK_NOOVERWRITE` 语义复用 D3D8 路径实现。
5. `IDirect3DVertexShader9`/`IDirect3DPixelShader9`：`CreateVertexShader`/
   `CreatePixelShader` 接收原始字节码，guest 端**只**做 token 计数与
   hash 计算，不解析语义（真正的解析/翻译在 host 端，见第 9 章）；
   `GetFunction` 返回原始字节码影子。
6. `IDirect3DTexture9`：`LockRect`/`UnlockRect`，格式转换表扩展自 D3D8
   路径（见第 11 章）。
7. `IDirect3DCubeTexture9`：`LockRect(face, level, ...)`，六个面各自的
   影子存储与独立脏区跟踪。
8. `IDirect3DVolumeTexture9`：`LockBox`/`UnlockBox`，三维脏区跟踪
   （比 2D 贴图的脏矩形多一维，需要新的影子存储结构，不能复用 2D 贴图的
   脏矩形代码）。
9. `IDirect3DSurface9`：`GetRenderTargetData`、`StretchRect`、
   `ColorFill`，只读回读遵循第 2.2 节的"精确来源"原则。
10. `IDirect3DQuery9`：`Issue`/`GetData`，第一版仅 `OCCLUSION`/`EVENT`。
11. `IDirect3DStateBlock9`：复用 D3D8 路径的 capture/apply 设计，扩展
    record 集合覆盖 sampler state、顶点声明、shader 常量。
12. `IDirect3DSwapChain9`：第一版只支持隐式（设备自带的）swap chain，
    `CreateAdditionalSwapChain` 返回 `D3DERR_INVALIDCALL`（三款目标游戏
    都是单窗口渲染，不需要多 swap chain）。

### 7.4 COM 语义与 capabilities 策略

与 D3D8 路径相同的纪律：未实现方法返回 `D3DERR_INVALIDCALL`，
`GetDeviceCaps`/`CheckDeviceFormat`/`CheckDeviceType` 只对已验证可用的
格式/能力返回"支持"。这一条在 D3D9 上更重要——WoW 在启动时会用 caps
探测结果选择渲染路径（例如是否启用 vertex shader 3.0 的顶点纹理采样），
caps 撒谎会导致游戏选中一条实际跑不通的路径而不是优雅降级。

### 7.5 shadow state 与重复设置抑制

延续 D3D8 路径的"重复 render state / texture stage state 抑制"设计，
扩展到 `SetSamplerState`（每个 sampler 槽位独立去重）和着色器常量
（按寄存器范围做脏区合并，避免同一常量被多次设置时产生多条
`SET_*_SHADER_CONSTANT_*` 命令）。

### 7.6 多线程策略

与 D3D8 路径相同：假设 D3D 调用发生在单一渲染线程上（`D3DCREATE_
MULTITHREADED` 标志只影响 guest 端内部锁，不改变协议模型），批处理状态
不做跨线程同步。

### 7.7 资源 pool 与 Lock/Unlock

延续 D3D8 路径的 `D3DPOOL_DEFAULT`/`MANAGED`/`SYSTEMMEM`/`SCRATCH`
处理规则；`D3DUSAGE_AUTOGENMIPMAP` 纹理在 host 端用 WebGPU 的 mip 生成
（`GPUCommandEncoder` 逐级 blit 或计算着色器降采样）实现，guest 端仅传递
该 usage 标志，不在 guest 侧生成 mip。

## 8. 浏览器端 WebGPU executor

### 8.1 建议模块

```text
glbridge/d3d9-webgpu/
  d3d9_executor.js         # D9WG 解码、资源/状态表、渲染
  d3d9_shader_pipeline.js  # 第 9 章字节码编译管线（可独立单测）
glbridge/webgpu-runtime/   # 新增，D3D8/D3D9 共享基础设施（见 3.1）
  webgpu_device.js
  lru_cache.js
```

M2 落地时前两个文件都已建立（`webgpu-runtime/` 仍未建）。注意加载顺序：
`d3d9_executor.js` 在**加载时**就解析 `D3D9ShaderPipeline`，不是惰性的，
所以 `d3d9_shader_pipeline.js` 的 `<script>` 必须排在它前面
（见 `game.html`）。这样写是为了让缺失依赖在页面加载时就报错，而不是
拖到第一次 `CreateVertexShader` 才炸。

### 8.2 初始化生命周期

与 D3D8 executor 相同的形状：独立 overlay canvas、独立 WebGPU
context，`HELLO`/`CREATE_DEVICE` 建立设备与会话命名空间，`RESET`
是"新纪元"而不是"就地修改"。

### 8.3 每个 batch 的执行流程

在 `SET_VERTEX_SHADER`/`SET_PIXEL_SHADER`/draw 命令处理路径上，pipeline
key 现在必须包含：vertex declaration 的属性布局签名、绑定的 vertex/pixel
shader handle（而不是"当前固定管线特性组合"）、独立采样器集合签名、
render target 格式集合（含 MRT 数量与各自格式）、多重采样参数。这比
D3D8 的 pipeline key 明显更大，必须做结构化哈希而不是字符串拼接，避免
key 计算本身成为热路径开销。

### 8.4 render pass 管理

新增 MRT 支持：一个 render pass 的 `colorAttachments` 数组长度等于当前
绑定的非空 render target 槽位数；`D3DRS_COLORWRITEENABLE1/2/3` 映射到
对应 attachment 的 `writeMask`。深度/模板附件的绑定规则与 D3D8 路径
一致。

WebGPU 的纹理坐标原点与 D3D9 一致（左上角为原点），不像 OpenGL/WebGL
是左下角原点。`d3d9-webgl` 项目因为目标是 WebGL，专门实现了"render-to-
texture 时自动做 Y 翻转"的逻辑来抹平这个差异；本方案走 WebGPU 不需要
这一层——渲染到贴图、再把该贴图当作采样源使用时，坐标语义与 D3D9 原生
行为天然一致，不需要额外的翻转 pass 或在 WGSL 里翻转 UV。唯一需要注意
的是最终呈现到 `<canvas>` 的 swap chain 表面本身的坐标约定，需要在
M1 阶段用一个非对称的测试图案（例如四个角颜色不同的矩形）验证一次，
而不是假设"两边都是左上角原点"就自动等价。

### 8.5 device lost

与 D3D8 路径一致的"从 guest 影子重建"策略；新增内容是 shader 编译缓存
在设备丢失后**不失效**（WGSL 源码与编译结果和具体 `GPUDevice` 实例无关，
只有 `GPUShaderModule`/`GPURenderPipeline` 这些设备绑定对象需要在新设备
上重新创建，翻译产物本身可以复用，避免设备丢失后需要重新跑一遍 vkd3d-
shader/Tint）。

## 9. Shader 字节码编译管线（核心难点）

这是本方案与 D3D8 路径差异最大、风险最高的部分，单独展开。

### 9.1 D3D9 字节码格式速览

D3D9 shader 字节码是一串 32 位 token：

- 首 token 是版本 token（`vs_1_1`..`vs_3_0`、`ps_1_1`..`ps_3_0`），与
  D8WG 现有约定一致（`code_tokens[0]` 就是版本 token）。
- 后续是指令 token 序列，指令带目的/源寄存器操作数，操作数携带寄存器
  类型（临时 `r#`、输入 `v#`、常量浮点 `c#`、常量整型 `i#`、常量布尔
  `b#`、地址 `a0`、循环计数 `aL`、纹理坐标、采样器 `s#`、谓词 `p0`、
  输出 `o#`/`oPos`/`oFog`/`oPts`/`oD#`/`oT#`）、写掩码、swizzle、源
  修饰符（negate/abs/bias/scale 等）、相对寻址标志。
- SM2.0+ 用 `dcl_` 伪指令声明输入/输出语义和采样器类型（2D/Cube/Volume），
  vs_3_0 的输出必须显式 `dcl_` 声明（不像 vs_1_1/2_0 隐式使用
  `oPos`/`oD0` 等固定寄存器）。
- 控制流指令：`if`/`ifc`/`else`/`endif`、`rep`/`endrep`、
  `loop`/`endloop`、`call`/`callnz`/`ret`、`break`/`breakc`。
- 结束哨兵 `D3DVS_END()`/`D3DPS_END()`，与 D8WG 现有约定一致：host 按
  token 计数确定边界，不扫描结束标记。

### 9.2 为什么手写翻译器不现实

D3D8 路径的 SM1.x 翻译器能手写，是因为 SM1.x 字节码是**无分支的直线
代码**：每条指令独立翻译成几行 WGSL，顺序拼接即可。SM2.0/3.0 有真正的
结构化控制流和函数调用，手写翻译需要：一个正确的基本块/控制流图构建
器、SSA 或等价的寄存器版本管理（避免 WGSL 里对同一变量的错误依赖顺序）、
相对寻址转成 WGSL 的动态数组索引、谓词指令转成条件赋值。这相当于重新
实现一个简化版的着色器编译器前端，工程量和正确性风险都远超"扩展一张
opcode 表"，且极易在边角指令组合上产生隐蔽的渲染错误（不崩溃，但颜色/
光照错误，比崩溃更难排查）。

> **2026-08-07 落地修正**：M2 实际走的**不是**本节推荐的 WASM 工具链，
> 而是一个手写的 JS 直译器（`glbridge/d3d9-webgpu/d3d9_shader_pipeline.js`）。
> 理由、代价与保留的替换接缝见第 15 章 M2 的状态记录与该文件顶部注释；
> 一句话概括：9.2 节的论证针对 SSA 目标（SPIR-V）成立，对 WGSL 这种
> 带可变变量、控制流结构与 D3D9 汇编一一对应的命令式语言不成立。
> 本节以下内容作为**日后若要替换后端**的设计依据保留。

### 9.3 推荐路线：借用成熟开源前端，只编译需要的部分到 WASM

**vkd3d-shader**（Wine 项目的一部分，LGPL-2.1-or-later）已经实现了完整
且经过大量真实 D3D9 游戏验证的 SM1-3 字节码解析、中间表示和 SPIR-V
后端。推荐：

1. 从 vkd3d-shader 裁剪出「D3D9 字节码解析 + IR + SPIR-V 输出」这条链路
   （不需要 vkd3d 的 Vulkan runtime 部分、不需要 HLSL 源码前端），
   用 Emscripten 编译为 WASM 模块。
2. 这个 WASM 模块作为**编译期/首次使用期**工具运行在浏览器主线程或专用
   Worker 里，输入原始字节码 + hash，输出 SPIR-V 二进制。

**Tint**（Chromium/Dawn 项目的一部分，BSD-3-Clause，本身就是 Chrome
WebGPU 实现里做 WGSL↔SPIR-V 转换的组件）编译为 WASM，把上一步的
SPIR-V 转成 WGSL 源码。

管线：

```text
D9WG CREATE_VERTEX_SHADER/CREATE_PIXEL_SHADER (原始字节码 + hash)
  -> [hash 命中缓存？] -> 是：直接用缓存 WGSL/GPUShaderModule
                       -> 否：
  -> vkd3d-shader(wasm)：D3D9 字节码 -> SPIR-V
  -> Tint(wasm)：SPIR-V -> WGSL
  -> device.createShaderModule(wgsl) + getCompilationInfo() 校验
  -> 写入 {hash -> {wgsl, module}} 缓存
```

### 9.4 备选/补充路线

**naga**（Rust 实现，MIT/Apache-2.0，wgpu 项目的 shader 翻译组件）目前
没有 D3D 字节码前端，只接受 WGSL/SPIR-V/GLSL 输入，因此如果 Tint 在
某些指令组合上转换效果不理想，naga 可以作为 SPIR-V→WGSL 的第二种实现
来源做交叉验证（同一份 SPIR-V 分别喂给 Tint 和 naga，比较渲染结果，
用于调试而非生产双运行）。不建议把 naga 设为生产路径的主选项，避免同时
维护两条 WASM 工具链的构建与更新负担。

**完全不推荐**：从零实现字节码到 SPIR-V/WGSL 的翻译器。这是在重新做
vkd3d-shader 已经做过的工作，而且 vkd3d-shader 的正确性是被大量真实
Direct3D 9 游戏（含 Wine/Proton 生态）验证过的，自研版本在启动阶段不可能
达到同等覆盖率。

### 9.5 何时执行编译，如何避免卡顿

- 编译只在 `CREATE_VERTEX_SHADER`/`CREATE_PIXEL_SHADER` 命令处理时触发
  一次，绝不在 `Draw*` 路径上触发。
- 编译是同步 CPU 计算（WASM），会阻塞该 batch 的处理；对 WoW 这种游戏
  内容加载时可能连续创建几十个新 shader 的场景，建议把编译工作放到
  独立 Worker（`vkd3d-shader`/Tint 的 WASM 实例跑在 Worker 里），
  `CREATE_*_SHADER` 命令允许"提交编译请求，标记 handle 为 pending"，
  真正 `Draw` 引用一个仍处于 pending 状态的 shader handle 时才等待
  （通常此时编译早已在后台完成）。这个 pending 机制在第一版可以先不做
  （同步编译，接受首次加载卡顿），作为 M6 性能收敛阶段的优化项而不是
  阻塞前面里程碑的前提条件。
- 编译结果需要持久化跨会话缓存（见第 19 章），否则每次刷新页面都要
  重新编译 WoW 加载时遇到的全部 shader，用户体验不可接受。

### 9.6 编译产物必须真实验证

`device.createShaderModule()` 之后必须调用 `getCompilationInfo()` 并
检查是否存在 `error` 级别消息；只有 `warning`/`info` 允许放行。绝不能
假设"字节码解析没报错就等于 WGSL 是对的"——这是 D3D8 路径的浏览器验证页
（`d3d8_webgpu_browser_test.html`）已经确立的纪律，D3D9 路径的等价页面
（第 18 章）必须延续。

### 9.7 常量寄存器与 uniform buffer 布局

D3D9 的常量堆比 D3D8 大得多且分三种类型（`c#`/`i#`/`b#`），vs_3_0 最多
256 个 float4 常量，ps_3_0 最多 224 个。WebGPU 侧只有统一的 uniform
buffer，需要设计一个打包布局：

```text
[0 .. float_const_count*16)          : c# 寄存器，逐 float4 紧凑排列
[float_region_end .. +int_count*16)  : i# 寄存器，按 int4 存储
[int_region_end .. +bool_count*4)    : b# 寄存器，每个 bool 占一个 32 位槽
                                        （WGSL 没有紧凑 bit-packed bool
                                        数组的原生支持，且这部分数据量很
                                        小，不值得为省内存增加解包开销）
```

vertex/pixel 各自独立一段 uniform，通过 dynamic offset 绑定（延续 D3D8
路径"uniform 走持久 ring + dynamic offset，不进 bind group identity"的
设计，这是它能保证"bind group 缓存不随 uniform 状态增长"的关键，D3D9
必须保留同样的性质，否则 WoW 级别的常量更新频率会让 bind group 缓存
失控增长）。

### 9.8 独立 sampler state 与纹理绑定

D3D9 的 sampler state 独立于 texture stage state，天然对应一个
`GPUSampler`；`dcl_` 声明里携带的采样器维度（2D/Cube/Volume）决定
`GPUTextureView` 的 `dimension`，绑定组布局需要按 shader 实际声明的
采样器集合生成，而不是像 D3D8 固定管线那样只有两个写死的纹理阶段。

### 9.9 vs_3_0 顶点纹理采样（高风险，后置特性）

`vs_3_0` 允许 vertex shader 采样纹理（`texldl` 等指令），WebGPU 规范
允许 vertex stage 采样纹理，但不是所有实现/硬件都同等支持（尤其是
`filterable` 采样在 vertex stage 的限制）。这个特性标记为 M6 阶段
「若目标游戏确实用到再实现」的可选项，不放进 M4/M5 的必要路径——阶段 0
的 API trace 需要明确回答 WoW 3.3.5a 是否真的用到顶点纹理采样（多数
情况下用于地形程序化位移，不是所有客户端版本都启用）。

### 9.10 不支持运行时 HLSL 源码编译

`D3DXCompileShader`/`D3DXCompileShaderFromFile` 需要一个完整的 HLSL
前端（词法/语法分析、类型检查、到字节码的代码生成），这本身是另一个
量级的编译器工程，且与"翻译已编译字节码"是完全不同的问题。第一版明确
不支持；如果阶段 0 的 API trace 发现某游戏在运行时编译源码（而不是加载
预编译 `.fxo`），需要单独立项评估是否引入
DirectXShaderCompiler/`fxc` 的兼容实现，并作为独立里程碑，不混入本方案
的时间线。

### 9.11 Clip Plane：用 fragment shader discard 模拟，不永久搁置

WebGPU 和 WebGL 一样没有硬件裁剪平面。`d3d9-webgl` 项目用一个廉价且
可直接照搬思路的办法解决了这个问题：在顶点着色器里把裁剪平面方程对
world-space 位置求点积算出一个带符号距离，作为一个 varying 传给片元
着色器，片元着色器里距离为负就 `discard`。这个技术不依赖任何 D3D9
特有的东西，可以原样搬到 WGSL：vertex shader 输出一个额外的
`@location` 标量，fragment shader 开头 `if (dist < 0.0) { discard; }`。

D3D8 路径的 README 把裁剪平面列为"未实现，caps 不声明支持"；D3D9 路径
不需要沿用同样的搁置决定——这是一个成本很低、不需要等字节码编译管线
（第 9.1-9.10 节）跑通就能做的功能，建议排进 M3（Warcraft III，如果
阶段 0 观测到用到）或 M5（WoW），而不是像 D3D8 路径那样长期留白。
`SetClipPlane` 对应新增一个 D9WG 状态设置命令，把平面方程系数
（4 个 float）传给 host，host 端把它作为每个受影响 pipeline 的一个
额外 uniform 参与 shader 生成，而不是一个全局渲染状态位。

## 10. Vertex Declaration 与 Input Assembly

- host 端把 `D9WGVertexElement` 数组直接映射为 WebGPU
  `GPUVertexBufferLayout[]`：`stream` 决定归属哪个 vertex buffer 槽位，
  `offset`/`type` 决定 `GPUVertexAttribute.offset`/`format`。
- `D3DDECLTYPE` → `GPUVertexFormat` 映射表（第一版覆盖实际出现的类型，
  按阶段 0 的枚举结果而非规格书全集）：`FLOAT1/2/3/4` →
  `float32/32x2/32x3/32x4`，`D3DCOLOR` → `unorm8x4`（注意 D3DCOLOR 是
  BGRA 字节序，WGSL 里读出后需要 swizzle，与 D3D8 路径处理 `DIFFUSE`
  颜色分量的方式一致）,`UBYTE4`→`uint8x4`,`UBYTE4N`→`unorm8x4`,
  `SHORT2/4`→`sint16x2/x4`,`SHORT2N/4N`→`snorm16x2/x4`,
  `FLOAT16_2/4`→`float16x2/x4`。不在此列的类型（`UDEC3`/`DEC3N` 等）
  在阶段 0 未观测到使用时保持未实现,`CreateVertexDeclaration` 直接拒绝。
- `D3DDECLMETHOD` 只接受 `DEFAULT`；`TESSELLATE`/`CROSSUV` 等 N-patch
  相关方法直接拒绝（三款目标游戏不使用硬件曲面细分）。
- `D3DDECLUSAGE` → WGSL `@location` 的映射沿用 D3D8 路径已经确立的
  "语义槽位固定分配"约定，扩展 `BLENDWEIGHT`/`BLENDINDICES`（顶点蒙皮，
  WoW 角色渲染必需）、`TANGENT`/`BINORMAL`（法线贴图相关效果）。
- FVF 兼容路径：guest 端把 `SetFVF` 翻译成一份等价 `D9WGVertexElement`
  数组后，通过与 `CreateVertexDeclaration` 完全相同的 D9WG 命令下发（即
  `D9WG_OP_SET_FVF` 只是语法糖，host 端处理逻辑与显式声明完全一致，
  不写第二套 input-layout 推导代码）。

## 11. Texture、Surface、Cube/Volume 与格式

### 11.1 格式矩阵（在 D3D8 已支持格式基础上新增）

延续已支持的 `A8R8G8B8`/`X8R8G8B8`/`R5G6B5`/`X1R5G5B5`/`A1R5G5B5`/
`A4R4G4B4`/`L8`/`A8`/DXT1/3/5，新增（按阶段 0 实际观测结果排优先级，
非观测到的格式保持不支持，caps 诚实反映）：

- `D3DFMT_A2R10G10B10`：转换到 `rgb10a2unorm`。
- `D3DFMT_G16R16`/`D3DFMT_R32F`/`D3DFMT_A16B16G16R16F`：浮点/高精度
  渲染目标，WoW 少数特效（如某些光照/HDR 中间缓冲）可能用到，按需实现。
- `D3DFMT_ATI2`（俗称 3Dc/BC5，法线贴图压缩）：WebGPU 对应
  `rg-bc5-unorm`（需确认目标浏览器的 `texture-compression-bc` 特性
  可用；不可用时走 CPU 解码回退，与现有 DXT 解码回退同样的策略）。
- 深度贴图作为着色器资源读取（部分特效读取深度缓冲做软阴影/雾效）：
  D3D9 时代常见的非官方 hack 格式（`D3DFMT_INTZ`/`DF16`/`DF24`）依赖
  厂商驱动私有扩展，**不承诺支持**；阶段 0 需要确认三款游戏是否依赖，
  若依赖则设计显式的"深度纹理"路径（WebGPU 原生支持深度纹理绑定为
  着色器资源，不需要 hack，只是需要 guest 端识别这个使用模式并请求
  正确的 usage）。

### 11.2 Cube Texture

`IDirect3DCubeTexture9` 六个面独立管理脏区和上传，映射为一个
`GPUTexture`（`dimension: "2d"`, `size.depthOrArrayLayers: 6`）配合
`GPUTextureView`（`dimension: "cube"`）用于采样、按面的
`dimension: "2d", baseArrayLayer: face` 视图用于渲染目标写入（如果游戏
把 cube map 的某一面设为渲染目标，例如动态环境反射）。

**2026-08-08（M3）落地**：采样路径已按上面的形状实现，`UPDATE_TEXTURE`
的 `z` 字段作为面索引。**把 cube 的某一面设为渲染目标尚未实现**——
`renderTargetsFor()` 建的目标视图固定 `baseArrayLayer: 0`，因为
`SetRenderTarget` 现在只按"纹理 + mip 级别"寻址，还没有面/层这一维。
需要它的时候要一起改协议里的 `D9WGSetRenderTarget`。
另外必须记下一条真实 WebGPU 才能抓到的约束：bind group layout 的 texture
条目要显式写 `viewDimension: "cube"`，默认的 `"2d"` 与 shader 里的
`texture_cube<f32>` 不匹配会让整条 pipeline 创建失败，而 `naga` 单独校验
一个 module 时看不到这件事。

### 11.3 Volume Texture

`LockBox`/`UnlockBox` 的三维脏区跟踪需要新的影子存储数据结构（不是
2D 脏矩形的简单扩展），映射为 `GPUTexture`（`dimension: "3d"`）。体积
贴图在三款目标游戏里预期使用面很窄（体积雾、少量特效），不作为早期
里程碑的阻塞项。

**2026-08-08（M3）：仍未实现。** `CreateVolumeTexture` 返回
`D3DERR_INVALIDCALL`，`D3DPTEXTURECAPS_VOLUMEMAP` 不上报，所以按 caps
行事的应用不会走到这里。协议侧已经预留：`CREATE_TEXTURE_VOLUME` 与
`D9WGUpdateTexture` 的 `z`/`depth`/`slice_pitch` 字段就是为它留的（cube
的面索引复用同一个 `z`，因为两者都只是选择上传落到哪一层）。翻译器与
固定管线级联对 `texture_3d<f32>` 采样的那一侧已经写好并过了 WGSL 校验，
缺的只是 guest 侧的 COM 对象与影子存储。

### 11.4 Multiple Render Targets 与格式独立性

`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS` 语义上允许各 render target
使用不同格式；WebGPU `colorAttachments` 天然支持每个 attachment
独立格式，这一条不需要额外设计，只需要 pipeline key 正确纳入每个
attachment 的格式（见 8.3）。

### 11.5 mipmap 与 AUTOGENMIPMAP

延续 D3D8 路径的显式 mip 上传；新增 `D3DUSAGE_AUTOGENMIPMAP` 由 host
端在纹理内容更新后用逐级降采样生成（简单双线性 box filter 足够，不需要
追求与原生驱动逐 bit 一致）。

### 11.6 禁止隐式 readback（原则延续）

与 D3D8 路径完全一致：CPU 影子精确覆盖"host 自己写入的内容"（上传、
`Clear`、`CopyRects`/`StretchRect` 已知源到已知目的），不覆盖"任意
GPU draw 产生的像素"。`GetRenderTargetData` 对后一类内容返回失败或
需要显式异步协议，不能静默返回陈旧/错误数据。

## 12. 独立 Sampler State 与多重渲染目标（补充说明）

已在 4.4、8.4、11.4 分别说明设计原则，这里补充实现顺序：**先做单
render target + 独立 sampler state（M4 前半），再做 MRT（M4 后半）**。
理由：WoW 登录/选角界面基本不需要 MRT，先把独立 sampler state 跑通即可
解锁一大批调用面；MRT 只在少数动态特效里出现，晚一点上线不阻塞主线。

## 13. Occlusion Query 与其他查询

**2026-08-08（M3）落地方式与本节原设计不同**：查询完全在 guest 侧回答，
给的是保守结果（EVENT 报已完成，OCCLUSION 报"全部样本可见"），没有任何
D9WG 流量，也没有用到 `GPUQuerySet`。理由写在 `d3d9_proxy.c` 的
`IDirect3DQuery9` 注释里：真正的 GPU 侧计数需要第 6.7 节的 host→guest
回传通道；在它存在之前，剩下的两个选择都比保守结果更糟——让
`CreateQuery` 失败会让引擎关掉一整条以查询为前提的分支，而一直返回
`S_FALSE` 会让极常见的 `while (GetData(...) == S_FALSE);` 轮询死循环。
高报可见性只会多画一些本可跳过的东西（损失帧率），低报会删掉该出现的
几何。本节以下内容作为**日后接上回传通道时**的设计依据保留。

- 第一版只实现 `D3DQUERYTYPE_OCCLUSION` 和 `D3DQUERYTYPE_EVENT`。
- Occlusion query 映射到 WebGPU `GPUQuerySet`（`type: "occlusion"`）；
  一个 query set 预分配固定槽位数（例如 256，超出时退化为"总是可见"而
  不是报错阻塞，避免遮挡剔除失效变成游戏崩溃）。
- `GetData` 语义见 6.7：非阻塞轮询为主，阻塞语义（`D3DGETDATA_FLUSH`）
  第一版不实现，返回未就绪状态即可（多数游戏的遮挡剔除逻辑本身就是
  "上一帧结果指导这一帧剔除"，能容忍轮询延迟一帧）。
- `D3DQUERYTYPE_TIMESTAMP`/`PIPELINETIMINGS` 等性能类查询不实现（游戏
  逻辑不依赖它们，只有调试工具会用）。

## 14. D3DX9 依赖评估

`d3dx9_xx.dll`（`_24` 到 `_43` 各版本号并存于 Windows 生态中）不是
D3D9 API 的一部分,而是微软提供的独立辅助库，常见用途：

- 纹理加载与格式转换（`D3DXCreateTextureFromFileInMemory`，支持
  BMP/JPG/PNG/TGA/DDS 解码 + 自动生成 mipmap）。
- Effect 框架（`.fx` 文件的运行时/离线编译与技术切换，`ID3DXEffect`）。
- 精灵批处理/字体绘制（`ID3DXSprite`/`ID3DXFont`，内部仍然是发出普通
  D3D9 draw call，不引入新的渲染语义）。
- 数学库（`D3DXMATRIX`/`D3DXVECTOR3` 等，纯 CPU 计算，与本方案无关）。

**评估结论与策略**：

1. 数学库部分与本方案无关，游戏静态链接或调用 DLL 均不影响 D9WG 协议。
2. `ID3DXSprite`/`ID3DXFont` 最终都落到普通 `Draw*` 调用，D9WG 路径
   不需要特殊处理，前提是它们内部使用的顶点格式/贴图格式在第 10、11
   章范围内。
3. 纹理加载（`D3DXCreateTextureFromFile*`）如果被游戏调用，说明游戏
   在运行时解码图片文件而不是直接读预转换好的 `.dds`；这部分**不经过
   D9WG 协议**，是纯 guest 端 CPU 工作（图片解码），只要 guest 端有
   可用的解码实现（复用 v86 XP 客户机里已有的图形库或提供一个精简的
   guest 侧 JPG/PNG/TGA 解码器），解码结果仍然通过标准
   `CreateTexture`+`LockRect` 路径进入 D9WG，不需要协议改动。这一项
   本身工作量不小，但边界清晰，可以独立于 D9WG 协议排期。
4. Effect 框架若只使用**预编译**技术（`.fxo`，游戏发行时已经把 HLSL
   编译成字节码），`ID3DXEffect` 内部对每个 pass 调用的仍然是
   `SetVertexShader`/`SetPixelShader`/`SetRenderState` 等标准 API，
   D9WG 路径不需要理解 `.fx` 技术/pass 语法本身。若游戏运行时编译
   `.fx` 源码，落回第 9.10 节的"不支持运行时 HLSL 编译"结论，需要
   单独立项。
5. **行动项**：阶段 0 必须为三款游戏分别确认它们链接的 D3DX9 版本号、
   实际调用的入口集合，以及 Effect 是否运行时编译。这直接决定是否需要
   一个独立的 `d3dx9_xx.dll` 转发/部分实现代理（类似 `openglproxy` 对
   `opengl32.dll` 的做法），该代理如果需要，属于与 `d3d9proxy` 平行的
   新增工作项，不在本文档的里程碑内估算工期，留待阶段 0 结果出来后
   单独排期。

## 15. 分阶段实施流程（按游戏里程碑）

### M1：协议骨架与传输层

- 冻结 D9WG v0.1（第 6 章），实现 `HELLO`/`CREATE_DEVICE`/`RESET`/
  `PRESENT`/`CLEAR`/`BeginScene`/`EndScene`。
- guest 端 `d3d9.dll` 骨架：`IDirect3D9`/`IDirect3DDevice9` 生命周期，
  批处理与 DMA 提交（复用 D3D8 路径验证过的时机策略）。
- host 端 `d3d9_executor.js` 骨架：资源表、WebGPU 设备生命周期（可以
  直接依赖新增的 `glbridge/webgpu-runtime/` 共享模块）。
- 顶点声明 + 简单固定管线 draw（无 shader，等价于 D3D8 的
  `XYZRHW`/`XYZ` 预变换路径）。
- 验收：**War3 能进入主菜单，静态场景（无特效）能画出来，颜色/贴图
  基本正确**（2026-08-06 起改为验收目标，原因见下方状态记录；不要求
  单机战役可玩——那是 M3 的验收线）。

**2026-08-06 状态记录**：guest DLL（`d3d9_proxy.c`）与 host executor
（`d3d9_executor.js`）已经按本节范围实现，并且用 5 个独立的真实环境
smoke test 逐条验证过（`glbridge/sample/d3d9_{clear,triangle,texture,
world,reset}_test.c`，构建命令见 `glbridge/d3d9proxy/build.sh --tests`）：
`Direct3DCreate9`/`CreateDevice`/`Clear`/`Present` 链路、顶点缓冲+`SetFVF`+
`DrawPrimitive`、纹理上传+索引缓冲+`DrawIndexedPrimitive`、真实
`IDirect3DVertexDeclaration9`+`SetTransform`（验证了 host 端矩阵转置/乘法
顺序不是碰巧对的）、以及 `Reset` 后 `D3DPOOL_MANAGED` 资源存活与状态
重绑定，五项在 v86 内真实的 XP 客户机里全部通过。这已经超出本节原始
验收要求的验证深度。

验收目标原定 KartRider（主菜单场景最简单），但 KartRider 走的私服卡在
联网这一步，跟渲染管线无关，短期内无法验证。因此把 M1 的验收目标换成
War3——War3（2002 年发布）的原始最低配置显卡（GeForce 2 级别）本身完全
没有可编程 shader，说明它天然带一条不依赖 SM1.x 的固定管线兜底路径。

**2026-08-07：M1 验收达成。** Warcraft III 1.27.0.52240（冰封王座）在
真实的 v86 XP 客户机里进入主菜单并正确渲染：Logo、3D 冰封尖塔场景、
六个菜单按钮、版本号全部正常。host 端统计：117 张纹理 / 723 次上传 /
约 20 MB 纹理数据、330038 次索引绘制、0 次丢弃绘制、0 个畸形批次、
11 条渲染管线（缓存命中 330027 次）。**这也印证了 4.7 节的判断——
War3 确实不需要等 M2 的 shader 编译管线就能跑到主菜单**（caps 上报
`VertexShaderVersion`/`PixelShaderVersion` 均为 0.0，游戏接受并走了
固定管线路径）。

从"命令能送达"到"画面正确"之间实测暴露并修复的问题，按发现顺序：

1. **缓冲区更新未按 4 字节对齐**——`writeBuffer` 要求 4 的倍数，而 D3D9
   的 Lock 区间没有此约束（16 位索引缓冲的局部更新是常见反例）。改为在
   host 端维护 CPU 镜像，把更新写进镜像后只重传覆盖它的 4 字节对齐区间。
2. **swapchain 贴图跨宏任务失效**——`getCurrentTexture()` 只在获取它的
   那个任务内有效，而 guest 会在 DMA 环形区写满时把一帧拆成多次 PCI
   提交。改为只记录轻量操作列表，把贴图获取/建 pass/提交全部推迟到
   Present 那一刻同步完成。
3. **WVP 矩阵乘法顺序颠倒**——存储和上传各转置了一次，净效果是
   `Wᵀ·Vᵀ·Pᵀ` 而正确形式是 `Pᵀ·Vᵀ·Wᵀ`。原 world smoke test 的 VIEW/
   PROJECTION 都是单位矩阵，恰好使顺序不可观测，因此没抓到；新测试改用
   三个互不交换的矩阵并与独立实现的参考结果逐点比对。
4. **`GetSurfaceLevel` 未实现（本次黑屏的直接原因）**——原注释断言
   "通过 `CreateTexture()`+`LockRect()` 创建的纹理不需要这个"，对 War3
   不成立：它经由 `GetSurfaceLevel` 取得层级表面再锁定写入。该调用失败
   导致 117 张纹理全部创建并绑定却从未写入一个字节，几何正常但采样恒为
   黑色。同时补上了 `UpdateTexture`（SYSTEMMEM→DEFAULT）这条等价路径。
5. **深度目标跨设备世代泄漏**——上一个进程未 Present 完的帧会把它的
   深度视图带进下一个进程的帧，尺寸不符时 WebGPU 拒绝整个命令缓冲。
   改为在 `CREATE_DEVICE` 时丢弃残留帧，并在提交前做尺寸兜底检查。

诊断方法上的一条教训值得记下：**guest 与 host 两侧都存在"静默失败"盲区**
——已实现但因参数不支持而返回 `D3DERR_INVALIDCALL` 的调用、以及 host 端
被 `if (...) return;` 吞掉的绘制，都不会留下任何痕迹，与"游戏根本没调用"
无法区分。开发阶段曾临时加入 guest 侧首次命中日志，并配合 host 侧
`noteDroppedDraw` 与 `droppedDraws` 计数定位问题；发布版已移除 guest
日志及其文件 I/O，只保留有界的 host 统计。`d3d9_executor.js` 里的
`debug.forceClearColor` /
`shaderMode` / `disableCull` / `disableDepthTest` 开关保留在代码中，用于
把"画面不对"快速二分成显示、几何、颜色来源等独立环节。

**M1 尚未覆盖、留给后续里程碑的**。渲染特性：固定管线光照（`SetLight`/
`SetMaterial` 只存不算）、texture stage state 的颜色/alpha 运算组合、
多于一个纹理阶段、独立 sampler state（`SetSamplerState` 只存不用）、
状态块、cube/volume 贴图、render target 与 MRT、查询对象。War3 主菜单
未依赖这些即可正确显示；进入实际战役场景后大概率会需要，届时按 M3
排期。

基础设施上还有三项本节清单提到或 D3D8 路径已具备、而 D3D9 侧尚未做的：

- **`glbridge/webgpu-runtime/` 共享模块未建立**。本节清单里写的是 host
  端"可以直接依赖"它，属许可而非强制，`d3d9_executor.js` 目前自带设备
  生命周期与缓存实现。等 M2 出现第二个消费者时再抽取，比现在为单一
  使用者预先抽象更合理（第 17 章的共享重构本就排在里程碑之后）。
- **没有 per-process session 隔离**。D3D8 路径用 64 位会话号防止不同 XP
  进程的同名数字 handle 互相踩踏，D3D9 侧目前只有一张扁平设备/资源表。
  单进程单游戏够用，但在把这条路径当作可信之前应当补上。
- **没有设备丢失恢复**。D3D8 侧能从 CPU checkpoint 重建，D3D9 侧目前只
  在 `device.lost` 时打一条错误日志。

另外实测中发现的一个尚未处理的现象：全屏模式（`windowed=0`）下 guest 侧
`GetClientRect` 返回空矩形，导致 `D9WGPresent` 携带的 width/height 为 0。
当前不影响显示（canvas 定位回退到自身尺寸），但全屏下的窗口尺寸获取需要
另行处理。

### M2：SM2.0/3.0 字节码编译管线上线

- 第 9 章整条管线：vkd3d-shader(wasm) → SPIR-V → Tint(wasm) → WGSL，
  hash 缓存、`getCompilationInfo()` 校验。
- 顶点/像素常量寄存器（F/I/B）与 uniform 打包布局（9.7）。
- 独立 sampler state（4.4、12）。
- 验收：KartRider 的水面/反光等 SM2.0 特效正确渲染，赛道可以从起点
  跑到终点，帧率达标（见 2.3 成功标准表）。

**2026-08-07 状态记录：代码与自动化验证已完成，真实游戏验收待人工执行。**

**翻译后端改为手写 JS 直译器，不走 vkd3d-shader/Tint 的 WASM 工具链。**
这是对 9.3 节推荐路线的有意偏离，理由见
`glbridge/d3d9-webgpu/d3d9_shader_pipeline.js` 顶部注释，核心是：9.2 节
"需要 CFG 构建器和 SSA 寄存器版本管理"的论证针对的是 **SPIR-V 这类 SSA
目标**，对 WGSL 不成立。D3D9 汇编和 WGSL 都是带可变变量的命令式语言，
且 D3D9 的控制流按规范就是结构化嵌套的（`if/endif`、`rep/endrep`、
`loop/endloop`、`call/ret`，没有任意跳转），恰好对应 WGSL 的
`if`/`loop`/函数——所以翻译是针对 `var<private>` 寄存器的逐语句转写，
不需要基本块、CFG、SSA 或 phi 节点。代价是正确性依靠本仓库自己的测试
而非 Wine 多年的游戏覆盖；收益是没有 Emscripten/Dawn 构建链要维护、
没有第 25 章标记为未解决的 LGPL 分发义务、且翻译器可以直接在 Node 里
单测。`compileShader(tokens)` 是接缝：日后换成 WASM 后端只动这一个文件。

已落地范围：

- **翻译器**（`d3d9_shader_pipeline.js`，约 1400 行）：vs/ps 1.1–3.0
  的指令集、写掩码、源/目的修饰符、swizzle、相对寻址（`c[a0.x+n]` /
  `c[aL+n]`，带越界 clamp）、谓词指令、`rep`/`loop`/`if`/`ifc`/`break`、
  `label`/`call`/`callnz`（按调用图拓扑排序成 WGSL 函数，检测递归并拒绝）、
  `def`/`defi`/`defb`、`dcl` 语义、vs_3_0 的任意输出语义、ps_3_0 的
  `vPos`/`vFace`/`oDepth`。ps_1_x 的 bump-environment 与 `texm3x*` 家族
  **明确拒绝**（`UNSUPPORTED_OPS`），不做近似。
- **VS/PS 接口契约**：固定管线与可编程着色器是**同一条路径**。两个阶段
  永远是两个独立的 `GPUShaderModule`，通过固定的 varying 约定相遇
  （COLOR0/1 = location 0/1，TEXCOORD0..7 = 2..9，FOG = 10），固定管线
  阶段被合成成遵守同一约定的模块。这是 D3D9 允许的四种 VS/PS 组合
  （含"固定管线 T&L + 真实 pixel shader"）能工作的前提。
- **常量寄存器**：guest 侧持有完整寄存器文件影子，抑制重复设置并把变化
  收窄到脏区间；host 侧按 9.7 打包（float4 区 → int4 区 → 每 bool 一个
  32 位槽），vs/ps 各占一段、pixel 段按 256 字节对齐以便独立绑定。
  shader 自带的 `def` 常量覆盖 app 设置的同号寄存器（D3D9 语义）。
- **独立 sampler state**：`SetSamplerState` 驱动一个按参数元组去重的
  `GPUSampler` 缓存，取代 M1 那个"随纹理创建、永远 linear/repeat"的
  采样器。`D3DTADDRESS_BORDER`/`MIRRORONCE` 无 WebGPU 对应物，退化为
  clamp 并各记一条 warning。
- **多 stream 顶点声明**：每个 stream 一个 `GPUVertexBufferLayout`，
  按 stream 号稳定排序。
- caps 诚实上报 `vs_2_0`/`ps_2_0`；`PS20Caps.DynamicFlowControlDepth`
  故意报 0（见下方"已知折衷"）。适配器身份同步改为 GeForce FX 5200
  （NV34，第一代支持 SM2.0 的 NVIDIA 入门卡），因为 M1 报的 GeForce4 MX
  没有可编程着色器，与 `(2,0)` 的 caps 自相矛盾。

**M2 过程中发现并修复的 M1 缺陷**（都不是新代码引入的）：

1. **`DrawIndexedPrimitiveUP` 每次调用必抛异常**——payload 里的 `stride`
   字段从未被读出，但记录绘制时引用了同名变量，严格模式下是
   `ReferenceError`，会被 batch 的 catch 吞掉并丢弃整帧。War3 主菜单
   恰好不走这个入口，所以 M1 没暴露。
2. **固定管线顶点属性 location 按元素顺序分配，而 WGSL 里写死为
   位置/颜色/纹理坐标 = 0/1/2**——只有当声明恰好按这个顺序排列时才一致。
   一个把 TEXCOORD 排在 COLOR 前面的声明会把纹理坐标的字节喂进颜色属性。
   改为按语义分配。
3. **图元拓扑一律按 `triangle-list` 建管线**，同时又按 strip/fan 的规则
   计算元素数量——N 个三角形的 strip 被当成 floor((N+2)/3) 个互不相干的
   三角形光栅化。这类错误画出来的是**错误的几何**而不是空白，很容易被
   误判成变换矩阵的问题。现按 D3DPRIMITIVETYPE 映射拓扑；WebGPU 没有
   fan 拓扑，转成生成的索引三角形列表（索引 fan 通过索引缓冲的 CPU
   镜像重新索引）；索引 strip 补上 `stripIndexFormat`。
4. **`DESTROY_RESOURCE` 立即 `destroy()` GPU 对象**——正在录制的帧可能
   已经持有引用该纹理视图的 bind group，而帧要到 Present 才提交，于是
   WebGPU 拒绝整个 command buffer（"Destroyed texture ... used in a
   submit"）。在同一帧里释放刚画过的纹理是普通应用行为，不是边角情况。
   改为走已有的 `retireGPUObject()` 延迟销毁路径。
5. **同一帧内改写动态缓冲会破坏该帧里更早的绘制（写后录制冒险）**——这条
   是 War3 真实运行中暴露的，也是本次最严重的一个。绘制被推迟到 Present
   才编码（因为 swapchain 贴图只在获取它的那个任务内有效），而
   `UPDATE_BUFFER` 是**立刻** `queue.writeBuffer` 的；`writeBuffer` 与
   `submit` 在队列上按调用顺序执行，于是一帧里所有的写都排在那唯一一次
   submit 之前，也就排在该帧**每一次**绘制之前。最常见的动态几何写法
   （`Lock(DISCARD)` → 填批次 A → 绘制 → `Lock(DISCARD)` → 填批次 B →
   绘制 → Present）因此让第一次绘制拿着正确的索引去读批次 B 的顶点。
   屏幕上的表现是：UI、文字、managed 资源渲染完全正常，而共享动态缓冲的
   场景几何飞出大片错位三角形，且每帧都不同——很容易被误读成"闪烁"。
   修复采用真实 D3D9 驱动对 `D3DLOCK_DISCARD` 的标准做法——**重命名
   （renaming）**：当被写的缓冲已经被本帧**已录制**的绘制读过时，分配一
   块新的 GPU 缓冲承载新内容，旧的留给先前的绘制并在提交后回收。判据是
   "本帧是否已被绘制引用"，所以"上传一次、绘制多次"这条常规路径一次都不
   会触发重命名。`bufferRenames` 计数暴露实际发生频率。纹理的同类冒险也
   已用按 mip/array-layer 保存的 CPU 影子完成重命名；旧 view 留给已录制
   的 bind group，新 view 供后续绘制使用，`textureUpdateHazards` 与
   `textureRenames` 分别统计检测和实际修复次数。

   遗留的性能风险：重命名会整份重传该缓冲的影子。对整块改写（DISCARD 的
   典型用法）不比 guest 本来发的数据更多，但"对一个大缓冲做小范围局部
   更新、且刚画过"会放大上传量。要先用 `bufferRenames` 量出真实频率再
   决定是否需要更精细的方案（例如在 Present 的 encoder 里用
   `copyBufferToBuffer` 只补未改动的部分）。

**验证情况**：

- `glbridge/tests/d3d9_shader_pipeline_test.js`：29 项，用手工汇编的真实
  D3D9 token 覆盖翻译结构（哪些寄存器、uniform、varying、reflection）。
- `glbridge/tests/d3d9_shader_wgsl_validation_test.js`：把 20 个语料
  shader 加 18 个合成的固定管线 shader 全部喂给 `naga` 做真实 WGSL 校验。
- `glbridge/tests/d3d9_webgpu_executor_test.js`：16 项，真实 D9WG 批次
  打进一个会强制执行 bind-group 与 `writeBuffer` 校验规则的假 WebGPU 设备。
- `glbridge/tests/d3d9_webgpu_browser_test.html`：**真实 WebGPU**（headless
  Chrome + CDP）下三次绘制通过，`pushErrorScope("validation")` 全程无错，
  其中第三次是翻译后的 `vs_2_0`+`ps_2_0`、走独立 sampler state 采样纹理。
  截图确认三个三角形都正确渲染。
- `glbridge/sample/d3d9_shader_test.c`：新增的 XP 客户机 smoke test，
  自带手工汇编的 vs_2_0/ps_2_0，覆盖 `CreateVertexShader`/`GetFunction`
  往返校验/常量设置与回读/`SetSamplerState`/真实顶点声明/同一帧内
  shader 与固定管线混用。已能编译，**尚未在 v86 里跑过**。

一条方法论上的收获：**naga 和 Tint 的严格程度不同，不能只用一个**。
翻译器最初把 f32 最大值写成 `3.4028235e38`——这是能往返 double 的最短
十进制，但解析后**大于** f32 上界。naga 接受，Tint 直接拒绝
（"cannot be represented as 'f32'"）。是那个跑真实 WebGPU 的浏览器测试
抓到的，改成十六进制浮点字面量 `0x1.fffffep+127` 才两边都过。

**2026-08-07 War3 首次真机运行的结论**（`guestShaderModel2: true`，2884 帧）：

- **War3 1.27 即使看到 `vs_2_0`/`ps_2_0` 的 caps 也一个 shader 都不创建**
  （`shadersTranslated: 0`、`programmableDraws: 0`，
  `constantUploadBytes ÷ indexedDrawCalls` 恰好等于固定管线 uniform 块的
  80 字节）。这不是缺陷，是这款 2002 年游戏的真实行为，也再次印证 4.7 节
  的判断。**推论：War3 无法作为 M2 shader 路径的验收目标**——它验证的是
  M2 顺带修好的固定管线正确性。SM2.0 路径的真实游戏验收仍然悬空，需要
  KartRider 或别的确实使用 SM2.0 的 D3D9 客户端。
- 全屏下 `GetClientRect` 返回空矩形是**每帧**发生的
  （`emptySurfaceReports` 等于 present 数），不是间歇性的，因此它不会
  造成画面抖动；M1 记录里"不影响显示"的判断成立。
- 真正的画面缺陷是上面第 5 条的写后录制冒险。

**尚未完成的**：本节的验收目标（原定 KartRider，因其私服联网问题改为
War3 进入实战场景）需要在真实 v86 XP 客户机里人工执行——War3 的镜像是
本地开发用的 `game/warcraft3.img`，按 `tests/site-configuration.test.js`
的断言刻意没有接进站点，`d3d9.dll` 也需要人工放进镜像里的游戏目录。
执行步骤：用 `glbridge/d3d9proxy/build.sh` 出的 DLL 覆盖游戏目录里的旧
版本，进战役，观察 host 端 `v86gl.d3d9Executor.getStats()` 的
`shadersTranslated`/`shaderTranslationFailures`/`drawsSkippedForBadShader`/
`shaderCompileErrors`/`droppedDraws` 五项。若画面出现回归，先用
`D9WG_CAPS_PROFILE=ffp` 把 caps 切到 M4.5 固定管线档，确认问题是否出在
shader 路径上；旧的 `D9WG_SHADER_MODEL=0` 仍作为兼容写法。

**2026-08-07 第二次真机运行后补的三项**（几何修好之后暴露出来的）：

- **D3D9 硬件光标**（`SetCursorProperties`/`SetCursorPosition`/`ShowCursor`，
  新 opcode `0x21A`-`0x21C`）。全屏 D3D9 游戏的指针走的是硬件光标而不是
  GDI，因此完全不会进入本站在 WebGPU 画布下面合成的 VGA framebuffer，而
  站点的 CSS 又用 `cursor: none` 藏掉了浏览器光标——三个入口都是 stub 时
  的结果就是指针彻底不可见（输入其实是好的，但看不见等于不能玩）。为此
  顺带实现了 `CreateOffscreenPlainSurface`（纯 CPU surface，带
  `LockRect`/`UnlockRect`），因为那是应用构造光标位图的常规途径；它被
  刻意限制在 32 位格式上，免得被当成通用离屏 surface 依赖。host 端把光标
  作为帧末一个独立的、无深度附件的 pass 画上去。D3D9 的硬件光标在
  `ShowCursor(TRUE)` 之后是**自动跟随系统指针**的（`SetCursorPosition`
  只用于强制移动），所以 guest 在每次 Present 时重新采样一次真实指针位置。
- **Alpha test**（`D3DRS_ALPHATESTENABLE`/`ALPHAFUNC`/`ALPHAREF`）。WebGPU
  没有对应的固定功能级，只能在 fragment shader 里 `discard`，因此比较函数
  与参考值进入 shader 变体和 pipeline key。固定管线与翻译后的 pixel shader
  都覆盖（后者按 alpha test 生成额外变体）。UI 图集和树叶广告牌普遍依赖
  它裁掉全透明像素；不实现的话那些像素会被画成不透明，表现正是"面板边缘
  /背景纹理不对"。
- **测试程序在 WM_PAINT 时重新 Present**。host 只从 Present 携带的
  client rect 学习 overlay 画布的位置，所以"渲染一帧就进消息循环"的程序
  在窗口被拖动后画面会留在原地。真实游戏每帧 Present 因而天然跟随；这是
  设计使然而非桥的缺陷，修的是我们自己的 smoke test，让它像正常 Windows
  程序一样重绘。

**2026-08-07 第三次真机运行后的修正**：

- **光标改为从 GDI 抓取，而不是只等 `SetCursorProperties`**。实测
  `cursorUploads: 0`——War3 从不调用 D3D9 硬件光标。原因是真实硬件上
  Windows 会把 GDI 光标合成到 primary surface 上（全屏 D3D9 也一样），
  游戏因此直接把指针交给系统；而这里的"primary surface"是 Windows 毫不
  知情的 WebGPU 画布，那次合成永远不会发生。改为 guest 每次 Present 用
  `GetCursorInfo` 检查当前 `HCURSOR`，变化时用 `GetIconInfo`+`GetDIBits`
  抓成 32bpp 送出（单色光标按 AND/XOR 掩码合成，无 alpha 的彩色光标用
  1bpp 掩码补 alpha）。应用如果自己调了 `SetCursorProperties`，它优先。
- **`bufferRenames` 的开销按 lock flag 分类解决**。上一轮量到每帧约 277
  次重命名。协议里 `D9WGUpdateBuffer.lock_flags` 一直带着这个信息，只是
  host 没读：`D3DLOCK_NOOVERWRITE` 本身就是"我写的字节没有任何已发出的
  绘制在读"的承诺——正好是这个冒险需要的保证，原地写即可，重命名纯属浪费
  （游戏正是用它往一个缓冲里连续追加批次的）；`D3DLOCK_DISCARD` 需要
  重命名，但新缓冲只需承载**本次写入的范围**，其余是应用已声明不再读的
  内容；两者都没有的普通 lock 才需要整份拷贝，很罕见。
  `bufferNoOverwriteWrites`/`bufferFullCopyRenames` 分别计数这两条。
- **修正上一轮 texture-stage 诊断的误报**。`drawsWithUnsupportedTextureOp`
  报了 108830（等于全部带纹理的绘制），实际是判据写错：War3 用的是
  `MODULATE(TEXTURE, D3DTA_CURRENT)`，而在 **stage 0，`D3DTA_CURRENT`
  按定义就是 diffuse**（没有前序阶段的结果可继承），与我们实现的
  `MODULATE(TEXTURE, DIFFUSE)` 完全等价。把两者视为不同参数导致了 100%
  的误报。结论随之改变：**texture stage 运算不是 UI 纹理问题的成因**。

**2026-08-08 War3 真机调试记录（M2 之外、但直接影响观感的一串修复）**：

按发现顺序，每一条都是先补诊断、再看数据、最后才改代码：

1. **全屏设备不切换显示模式**。真实 D3D9 独占全屏由 runtime 负责
   `ChangeDisplaySettings`；我们没做，于是 guest 桌面停在 1024x768 而游戏
   按 800x600 排版，窗口矩形取不到、点击落到别的窗口上。补上模式切换与
   `SetWindowPos`/前台抢占后，`emptySurfaceReports` 从 100% 归零。
   **2026-08-08 补充**：`CreateDevice` 时抢一次前台是不够的。极品飞车 9 跑到
   主菜单时每一次窗口状态上报都是 `foreground: false`——画面完美，每一次点击
   都落到别的窗口上。两个成因都不体现在画面里：游戏在设备之后又创建/激活了
   另一个窗口（启动画面、launcher、message-only 窗口），以及 **Windows 直接
   拒绝 `SetForegroundWindow`**（对非前台进程它返回 FALSE 且什么都不做）。
   因此改为在 Present 路径上按 500ms 间隔重新抢占（只对全屏设备——窗口化游戏
   每帧抢焦点是不可接受的），并用 `AttachThreadInput` 短暂挂到当前前台线程的
   输入队列上，这是绕过那条限制的既定做法（用完必须立刻脱开，否则两个消息
   队列耦合会一起死锁）。真实独占全屏 D3D9 设备本来就会在整个生命周期里
   持有前台并在激活时重新获取，所以这是在对齐 runtime 而不是跟用户抢。
2. **固定管线不应用纹理坐标变换**（`D3DTSS_TEXTURETRANSFORMFLAGS` +
   `D3DTS_TEXTURE0`）。War3 用 `D3DTTFF_COUNT2` 做云雾/冰棱的坐标动画，
   忽略它的结果是这些面渲染成平色块。注意坐标以**行向量 `(u, v, 1, 1)`**
   进矩阵——这正是游戏把滚动偏移放在第 3 行（`_31`/`_32`）的原因。
   目前只实现 stage 0 的 COUNT1/COUNT2，COUNT3/COUNT4/`PROJECTED` 仍计数
   告警。
3. **D3D9 硬件光标 + `CreateOffscreenPlainSurface`**（新 opcode
   `0x21A`-`0x21C`）。实测 War3 两个光标 API 都不调、而是把指针当几何体
   画，所以这条对它是死代码；但对确实使用硬件光标的游戏是必需的，保留。
   GDI 光标抓取作为回退在当时默认关闭；这条默认值已被 2026-08-09 的战役
   验收推翻，现在默认启用，只有 `D9WG_GDI_CURSOR=0` 才关闭。
4. **压缩纹理 `rowsPerImage` 传的是像素行数**而非块行数（BCn 块是 4x4），
   规范错误，已修。

这一串里真正的教训不是任何一条具体的 bug，而是**它们全都被"看起来没问题"
掩盖过**：静默的 `return D3DERR_INVALIDCALL`、只检查 `COLOROP` 却不检查
`TEXCOORDINDEX`/`TEXTURETRANSFORMFLAGS` 的"未支持操作"计数、64x64 的贴图
预览上限（要找的光标图集比它大）、`BLEND_FACTORS[x] || "src-alpha"` 这种
静默回退、以及把"UV 图是平滑渐变"当成"UV 是正确的"（那是**未变换**的原始
坐标，平滑本来就是它应有的样子）。共同点是：**声称"没问题"的那句话，其
检查范围比实际被忽略的范围窄。** 因此现在的纪律是：任何 `|| 默认值` 或
静默降级都必须同时带计数器和一次性日志；任何"未支持"计数在被引用为证据
之前，要先确认它覆盖的状态集合。相应地，`getStats()` 现在带有
`drawsWithUnmappedBlend`/`drawsWithTexCoordIndex`/`drawsWithTextureTransform`/
`drawsWithIncompleteMipChain`/`bufferRenames` 等一批"我们悄悄做了替代"的
计数，以及 `debug.dumpSmallTextures()`/`dumpPipelineStates()`/
`forceMipLevel0`/`shaderMode` 这些不用重新构建就能二分的开关。

**已知折衷（不是缺陷，但要记下来）**。下面几条按 M3（2026-08-08）之后的
实际状态更新过，M2 当时写的几条已经不再成立：

- **像素着色器里的数据相关分支会退化为 mip level 0 采样**。WGSL 禁止在
  非一致控制流里做隐式求导采样，而分支内部没有可以把 `dpdx`/`dpdy`
  外提到的坐标。退化路径是合法且确定性的（画面偏锐利/有走样，而不是
  颜色错误），并且被计数。ps_2_0 根本没有流控制、`if b#`/`rep`/`loop`
  这类由 uniform buffer 驱动的分支仍算一致控制流，所以这条路径对
  M2 的目标游戏实际上不可达——`PS20Caps.DynamicFlowControlDepth` 报 0
  正是为了不去招惹会命中它的 shader。
- ~~**MRT 的 `oC1`-`oC3` 被丢弃**，只写 `oC0`（MRT 本体在 M4）。~~
  **M3 已解决**：翻译器按 shader 实际写入的最高 `oC#` 生成连续的
  `@location` 输出，host 端绑定至多四个颜色附件。
- ~~**`UBYTE4`/`SHORT2`/`SHORT4`/`UDEC3`/`DEC3N` 顶点格式仍被拒绝**。~~
  **M5 已解决**：前三种分别用 `uint8x4`/`sint16x2`/`sint16x4` 取入并在
  declaration-specific WGSL variant 中扩展为 D3D9 规定的未归一化 float4；
  后两种以 `uint32` 取入，在 WGSL 中解包 10:10:10（`DEC3N` 同时做符号扩展
  和 snorm 归一化）。variant key 同时包含 BGRA swizzle 与紧凑格式转换，
  不会污染同一 shader 的其他顶点声明。
- **vs_3_0 顶点纹理采样**按 9.9 节明确拒绝（`dcl sampler` 出现在 vertex
  shader 里直接翻译失败），caps 里 `VertexTextureFilterCaps` 报 0。这是
  WoW（M5）的需求，M3 的两个目标游戏都用不到，仍未做。
- M1 遗留的三项基础设施中，per-process session 隔离与设备丢失恢复已在 M3
  补上（见 M3 状态记录里对后者范围的说明——它不等于完整恢复）；
  `glbridge/webgpu-runtime/` 共享模块仍未抽取。

### M3：Warcraft III 固定管线与地形

- 多层贴图混合（terrain splatting，通常是多纹理阶段固定管线运算或
  简单 SM1.x 等价 shader，复用第 9 章管线即可，不需要新设计）。
- 立方体/体积贴图（若 War3 实际用到，按阶段 0 结果决定是否本阶段做）。
- 状态块（`IDirect3DStateBlock9`）完整覆盖 D3D9 新增状态。
- 验收：单机战役任意一关可从过场到目标达成，地形/单位/UI 渲染正确。

**2026-08-08 状态记录：代码与自动化验证已完成，真实游戏验收待人工执行。**

本轮的核心认识是：M1/M2 结束时 host 端**存了**固定管线光照与全部
texture stage state，但一条都没有**用**。`SetLight`/`SetMaterial`/
`LightEnable` 自 M1 起就在线上传输，host 只记进 map；像素阶段则硬编码为
"stage 0 的 `MODULATE(texture, diffuse)`"。同时 `fill_caps()` 一直在上报
`MaxTextureBlendStages = 8`、`D3DVTXPCAPS_DIRECTIONALLIGHTS/
POSITIONALLIGHTS`、`MaxActiveLights = 8` 以及一大串 `TextureOpCaps`——
**这些是没有兑现的 caps 承诺**，正是本仓库反复强调要避免的那类问题。
M3 的主体工作就是把它们变成真的。

已落地范围：

- **固定管线光照**（`d3d9_executor.js` 的 `lightingSignature` /
  `buildFixedFunctionVertexShader` / `writeFixedVertexUniforms`）：在**视图
  空间**计算（D3D9 固定管线本来就在那里做），点光源/聚光灯/平行光三种类型
  全部支持，含距离衰减与 range 截断、聚光锥（`cos(theta/2)`/`cos(phi/2)`
  与 falloff 幂）、`D3DRS_SPECULARENABLE` 的高光项与 `D3DRS_LOCALVIEWER`
  两种视线近似、`D3DRS_AMBIENT` 全局环境光、`D3DRS_COLORVERTEX` 与四个
  `D3DRS_*MATERIALSOURCE` 的材质来源选择、`D3DRS_NORMALIZENORMALS`。
  **每个启用光源的类型烘进 shader 变体**，所以生成的 WGSL 是直线展开的，
  没有分支也没有动态光源数量。灯光位置/方向由 JS 侧在打包 uniform 时乘上
  view 矩阵，而不是把第二个矩阵搬进 shader 逐顶点重算。
- **声明里没有 NORMAL 时使用零法线，而不是跳过整条光照路径**。D3D9 对这类
  顶点的光照点积使用 0，所以各光源的 diffuse/specular 项消失，但
  `D3DRS_AMBIENT * material.ambient` 与 material emissive 仍然有效。旧实现直接
  让顶点色通过，会把 GTA San Andreas 中 `COLOR0` 为黑、依靠环境光显示的角色
  批次画成纯黑剪影。计数器 `drawsWithZeroNormalLighting` 暴露这类绘制频率。
- **完整的 texture stage 级联**（`textureCascadeSignature` /
  `buildFixedFunctionPixelShader`）：stage 0..N-1（N 止于第一个
  `D3DTOP_DISABLE`），每级独立的颜色/alpha 运算，参数取自 diffuse、级联
  中间结果（`CURRENT`）、本级贴图、`D3DRS_TEXTUREFACTOR`、specular、暂存
  寄存器（`D3DTA_TEMP` + `D3DTSS_RESULTARG`）与本级 `D3DTSS_CONSTANT`，
  含 `D3DTA_COMPLEMENT`/`D3DTA_ALPHAREPLICATE` 两个修饰位，每级结果按 D3D9
  语义饱和。实现的运算**恰好等于** `TextureOpCaps` 上报的那一套；
  `PREMODULATE` 与 `BUMPENVMAP` 家族既不上报也不实现，遇到时计数告警而
  不做近似。地形多层混合、细节贴图、光照贴图就是由这些拼出来的。
- **纹理坐标的来路终于对齐 D3D9**：每个 stage 一条 varying，`TEXCOORDINDEX`
  选择输入坐标集、`D3DTSS_TCI_*` 的三种相机空间生成模式（法线/位置/反射
  向量）、`D3DTS_TEXTURE0+n` 的 COUNT1..COUNT4 变换、`D3DTTFF_PROJECTED`
  的除法，全部实现（`SPHEREMAP` 仍计数告警）。M2 只做了 stage 0 的
  COUNT1/COUNT2。绑定了可编程 vertex shader 时不做生成/变换——D3D9 也不做。
- **立方体贴图**：真正的 `IDirect3DCubeTexture9`（六面 × 每级），host 侧是
  一张六层 2D 纹理加一个 `cube` 视图；`UPDATE_TEXTURE` 的 `z` 字段作为面
  索引（体积贴图将来用同一字段作切片，因为两者都只是选择上传落到哪一层）。
  固定管线级联与翻译后的 `dcl_cube` 都能采样它。
- **`SetScissorRect`** + `D3DRS_SCISSORTESTENABLE` 门控（`RasterCaps` 补上
  `D3DPRASTERCAPS_SCISSORTEST`）。
- **状态块**：`CreateStateBlock(ALL/PIXELSTATE/VERTEXSTATE)` 按 D3D9 文档
  的状态分组捕获（两张 render state 表照抄规范，捕获得比 D3D9 多不是安全的
  简化——`Apply` 会把 app 故意保留的状态一起还原回去），`Apply`/`Capture`
  完整，`BeginStateBlock`/`EndStateBlock` 用"Begin 处全量快照 → 正常执行 →
  End 处 diff 并还原"实现。与 D3D9 的差异（写入同值或写后改回的状态不会被
  记录）写在 `d3d9_proxy.c` 里 `IDirect3DStateBlock9` 的注释上，不留给后人
  自己发现。这样做的理由是：真 D3D9 在录制期拦截每一个 setter，而那需要在
  二十多个 setter 里各加一个绝不能忘的分支——忘掉任何一个都会静默丢状态。

**本轮把 M4 的一部分提前做了**，因为极品飞车 9（2005）这类游戏一帧里的
绝大部分内容是画进纹理再合成的，没有渲染目标它根本出不了画面：

- **渲染目标 / 深度表面 / MRT**：`CreateRenderTarget`、
  `CreateDepthStencilSurface`、`D3DUSAGE_RENDERTARGET` 纹理 +
  `GetSurfaceLevel`、`SetRenderTarget`（4 个槽位）、
  `SetDepthStencilSurface`、以及对应的 Get。翻译器现在按 shader 实际写入的
  `oC0..oC3` 生成多个 `@location` 输出（原先只写 oC0 并记一条 note），
  `D3DRS_COLORWRITEENABLE1/2/3` 映射到各附件的 writeMask。host 端的帧模型
  改为**按目标分组成多个 render pass**：每个记录下来的 op 携带它当时解析出
  的目标集合，`finishFrame` 把连续同目标的 op 合成一个 pass，Clear 总是开
  新 pass。`SetRenderTarget(0, ...)` 按 D3D9 语义顺带重置 viewport——不做这
  一步的典型症状是"渲染到纹理的那一遍只填了一个角"。
- **一个必须补的接口**：`GetDepthStencilSurface` 原本对隐式 auto
  depth-stencil 返回失败。看起来无害（没人读深度像素），实际会打断标准的
  RTT 序列——取不到当前深度表面的 app 就还不回去，于是第一遍 RTT 之后每一次
  绘制都在没有深度缓冲的情况下跑。现在返回一个标记为"隐式深度表面"的
  surface，协议上用 `D9WG_AUTO_DEPTH_STENCIL_HANDLE` 哨兵与
  `SetDepthStencilSurface(NULL)`（真的要关深度测试）区分开。
- **`StretchRect` / `ColorFill`**。`StretchRect` 分三档：等尺寸同格式且两侧
  都不是后台缓冲 → 真正的 `copyTextureToTexture`（无 pass 无 shader）；其余
  → 一次 blit pass（全视口四边形采样源，`setViewport` 限定目标矩形），覆盖
  缩放、格式转换和后台缓冲；压缩格式作为目标仍不实现并计数（BCn 不能当
  render attachment，没有东西可画进去）。~~部分矩形的 `ColorFill` 明确
  不实现并计数。~~ M4 已用 viewport 限定的全屏三角形 draw 实现局部填充，
  保留矩形外像素；带 rect list 的 `Clear` 也使用同一类独立 pass 完成颜色、
  深度与 stencil 的按位选择性局部清除。
- **涉及后台缓冲的 blit 必须延迟到 Present**，与绘制同理：swapchain 贴图只在
  获取它的那个任务内有效，而一帧会跨多次 PCI 提交到达。第一版在命令到达处
  立刻执行，结果是 NFS9 每帧报一条"host 无法寻址这个 surface"——**后台缓冲
  在那个时刻确实还没有 view**。这不是边角情况：把整帧抓进纹理再处理回去，
  正是 D3D9 游戏做全屏后处理的标准手法。顺带需要两处配套改动：
  `context.configure()` 必须显式申请 `COPY_SRC`/`COPY_DST`/
  `TEXTURE_BINDING`（画布上下文默认只有 `RENDER_ATTACHMENT`），以及非压缩
  纹理要带上 `RENDER_ATTACHMENT` 才能当 blit 目标。另外后台缓冲的格式
  （`getPreferredCanvasFormat`，通常 `bgra8unorm`）与 D3D9 纹理统一成的
  `rgba8unorm` 不同，所以**即使等尺寸的后台缓冲拷贝也走不了
  `copyTextureToTexture`** —— 这一条只有真实浏览器能验证。
- **遮挡/事件查询**：完全在 guest 侧回答，给的是**保守结果**（EVENT 报已
  完成，OCCLUSION 报"全部样本可见"）。理由写在 `d3d9_proxy.c` 里：让
  `CreateQuery` 失败会让引擎关掉一整条以查询为前提的分支；一直返回
  `S_FALSE` 会让极常见的 `while (GetData(...) == S_FALSE);` 轮询死循环。
  高报可见性只会多画一些本可跳过的东西，低报会删掉该出现的几何。真正的
  GPU 侧计数需要第 6.7 节的 host→guest 回传通道，仍未做。

**M1 遗留的基础设施，本轮补上两项：**

- **per-process session 隔离**：`D9WGHello` 的 64 位会话号现在真的被检查。
  不同 XP 进程的数字 handle 只在各自进程内唯一，此前第二个进程的
  `CREATE_BUFFER` 会静默覆盖第一个进程的表项，然后第一个进程拿着自己的
  索引去读别人上传的顶点。会话号变化时释放上一个进程的全部设备与资源
  （`sessionChanges` 计数）。
- **设备丢失恢复**：此前 `device.lost` 只打一条日志，之后每个 batch 永久
  失败——屏幕上就是一块冻住的画布，没有任何解释。现在会重新拿一个
  `GPUDevice`、重建 context 与缓存。**范围要说清楚**：翻译产物不受影响
  （WGSL 文本与 `GPUDevice` 无关，第 8.5 节），但 guest 的资源内容在 guest
  的 CPU 影子里，只有 guest 自己 Reset 时才会重放，而没有任何东西告诉它
  设备丢过——所以丢失之后的绘制会引用新设备没见过的 handle 并计入
  `droppedDraws`。这条把"页面死了"变成"页面活着且统计说清了发生什么"，
  仅此而已；真正的完整恢复要等第 6.7 节的回传通道。

**验证情况**：

- `glbridge/tests/d3d9_shader_wgsl_validation_test.js`：107 个 shader 过
  `naga`，其中新增的固定管线用例覆盖三种光源类型 × specular/localviewer
  组合、三种材质来源、四种 TCI 模式 × 五种变换分量数、三种雾模式 ×
  range、`TextureOpCaps` 里每一个运算的颜色与 alpha 两条路径、参数池与两个
  修饰位、暂存寄存器、2d/cube/3d × PROJECTED。
- `glbridge/tests/d3d9_webgpu_executor_test.js`：39 项（新增 9 项），含
  光照 uniform 的逐字段校验（灯光位置必须已经乘过 view 矩阵——这里刻意用了
  带平移的 view，因为单位矩阵会让"变换过"和"原样透传"无法区分，那正是 M1
  WVP 顺序 bug 的成因）、无法线时的顶点色透传、三级级联的绑定与
  `current`/`temp` 串联、`RESULTARG`、超出 caps 的运算被计数而非臆造、
  渲染目标切换导致的独立 pipeline 与 pass、立方体六面各自落到自己的层、
  scissor 的门控、以及会话切换释放旧进程资源。
- `glbridge/tests/d3d9_webgpu_browser_test.html`：**真实 WebGPU**（headless
  Chrome）下五次绘制通过，`pushErrorScope("validation")` 全程无错。第四次
  绘制是"固定管线光照 + 两级级联（其中一级采样立方体贴图、坐标由相机空间
  反射向量生成）+ 画进渲染目标"，第五次把该渲染目标当纹理采样回来。
- `glbridge/tests/d3d9_shader_corpus_test.js`（新增）：`D9WG_DUMP_SHADERS=1`
  的离线消费端，见下。

**一条又一次被印证的方法论**：`naga` 和 Tint 的严格程度不同，只用一个不
够。这次是 Tint 抓到的：bind group layout 的 texture 条目必须显式声明
`viewDimension`，默认的 `"2d"` 与 shader 里的 `texture_cube<f32>` 不匹配，
整条 pipeline 创建失败。`naga` 单独校验一个 module，看不到"module 与
layout 配对"这件事，所以它一路放行。修法是把每个采样槽位的维度带进
pipeline key 与 layout，并为每种维度准备一张 1x1 白色兜底纹理（原先只有
2D 那一张，绑定到 cube 槽位同样会失败）。

**仍未做的（本轮明确不做，不是忘了）**：

- **体积贴图**（`IDirect3DVolumeTexture9`/`LockBox`）。协议里的
  `CREATE_TEXTURE_VOLUME` 与 `UpdateTexture.z` 已经为它留好，
  `D3DPTEXTURECAPS_VOLUMEMAP` 保持不上报。两个目标游戏都不以它为主，
  优先级低于渲染目标。
- **用户裁剪面**。`MaxUserClipPlanes` 仍报 0（因此不会有 app 去用它）。
  WGSL 没有 clip distance，按 9.11 节必须变成"顶点算距离 + 片元
  discard"，而这会改动两个阶段共同遵守的 varying 契约，需要同时改翻译器
  与固定管线两侧。
- ~~**`UBYTE4`/`SHORT2`/`SHORT4`/`UDEC3`/`DEC3N` 顶点格式**仍被拒绝。~~
  M5 已用声明相关的 WGSL 输入转换完成。
- **vs_3_0 顶点纹理采样**（9.9）仍拒绝，`VertexTextureFilterCaps` 报 0。
  这是 WoW（M5）的需求，两个目标游戏都用不到。
- ~~**纹理的写后录制冒险**（`textureUpdateHazards`）仍只计数不修复。~~
  已实现纹理重命名；`textureFullCopyRenameBytes` 暴露重传成本。
- **`glbridge/webgpu-runtime/` 共享模块**仍未抽取（第 17 章本就排在里程碑
  之后）。

**验收待办**：与 M2 一样需要在真实 v86 XP 客户机里人工执行。War3 进战役
观察 `getStats()` 的 `drawsWithZeroNormalLighting`/
`drawsWithUnsupportedTextureOp`/`drawsWithTexCoordIndex`/
`drawsWithTextureTransform`/`droppedDraws`；极品飞车 9 额外观察
`renderTargetsCreated`/`renderTargetBinds`/`renderPasses`/
`blitsSkipped`/`shadersTranslated`/`shaderTranslationFailures`。
`blitsSkipped` 非零说明它依赖了压缩格式目标的 `StretchRect` 或部分矩形
`StretchRect` 的压缩目标或其他真实缺口；部分 `ColorFill` 已不再进入该计数。
`blitsThroughBackBuffer` 则量出这个游戏到底有多依赖抓帧后处理。

**2026-08-08：M2 的验收终于兑现，验收目标是极品飞车 9（Most Wanted, 2005）。**

M2 的状态记录里写着"SM2.0 路径的真实游戏验收仍然悬空"——War3 一个 shader
都不创建。NFS9 在真实 v86 XP 客户机里**跑完了一整场比赛**（结算画面
"WINNER 3:10.54 @ 109 MPH"），1333 帧的统计把这条补上了：

| 项 | 值 | 说明 |
| --- | --- | --- |
| `shadersTranslated` | 284 | 真实游戏的 SM2.0 字节码 |
| `shaderTranslationFailures` / `shaderCompileErrors` | 0 / 0 | 手写翻译器一条都没崩 |
| `programmableDraws` | 157242 | 几乎全部绘制走可编程路径 |
| `pipelineCreations` / `pipelineHits` | 19 / 157223 | pipeline key 收敛得很好 |
| `renderTargetsCreated` / `renderTargetBinds` / `renderPasses` | 7 / 18148 / 10776 | 每帧约 8 个 pass，13 次 RT 切换 |
| `blits` / `blitsThroughBackBuffer` / `blitsSkipped` | 1048 / 1048 / 0 | 抓帧后处理，全部经过后台缓冲 |
| `droppedDraws` / `malformedBatches` / `texturesRejected` | 0 / 0 / 0 | |
| `bufferRenames` | 2363 / 1333 帧 ≈ 1.8 | 写后录制冒险的实际频率，很低 |
| `constantUploadBytes` | 62.6 MB / 1333 帧 ≈ 47 KB/帧 | |

**统计口径的一个坑要记下来**：`drawsWithTexture: 0` 不是"没有贴图"，而是那个
计数器只在固定管线级联路径上自增，而 NFS9 基本不走固定管线。同理
`drawsWithUnsupportedTextureOp: 0`/`drawsWithUnappliedLighting: 0` 对这款游戏
是**空集上的真**，不构成 M3 固定管线工作的验证——那仍然要靠 War3。

**首次真机运行暴露的问题，按发现顺序**（每一条都是"画面看起来没问题"掩盖过的）：

1. **`texture-compression-bc` 从未申请**。`initialize()` 里是裸的
   `requestDevice()`，而 WebGPU 的可选 feature 不申请就一个都没有。DXT 格式
   自 M1 起就在格式表里，于是 `createTexture` 抛异常、穿过 batch、整帧被丢弃
   ——而这个年代的游戏几乎所有美术资源都在 DXT 里，症状是黑屏而不是缺贴图。
   顺带把纹理创建包进 `createTextureOrNull()`：一张纹理被拒绝只该损失一张
   纹理（退回白色兜底并计入 `texturesRejected`），不该抹掉整帧。
2. **涉及后台缓冲的 `StretchRect` 在命令到达处立刻执行**。那个时刻后台缓冲
   还没有 view（swapchain 贴图只在获取它的那个任务内有效），所以每帧报一条
   "host 无法寻址这个 surface"。改为推迟到 Present 的 blit op。详见上面
   M3 状态记录里的三档表。
3. **前台只在 `CreateDevice` 抢一次**。见上面第 1 条的 2026-08-08 补充。
4. **`D3DSAMP_SRGBTEXTURE` 被无人读取、也无人计数**。D3D9 会把 sRGB 标记的
   纹理在**读取时**解码到线性；忽略它等于把明显偏亮的值喂给 shader
   （sRGB 0.5 的线性值是 0.21，中间调差了两倍多），叠在环境反射这种加性项
   上的观感就是**过曝成白色**，而不是"gamma 有点不对"。WebGPU 没有
   sampler 级的 sRGB——它是纹理**格式**的属性——所以实现方式是建纹理时声明
   `viewFormats: ["...-srgb"]`，绑定时按该 stage 的 sampler state 选择用哪个
   view。`D3DRS_SRGBWRITEENABLE`（写回时编码）也已在 M5 完成：canvas 与
   可渲染纹理创建时声明兼容的 `-srgb` view，目标解析与 pipeline key 按该
   render state 选择线性或 sRGB 格式；没有 sRGB sibling 的目标会计入
   `srgbWriteUnavailable`，不会静默假装已经编码。

**并且新增了一条纪律性设施：未被读取的状态审计。** 这条路径上代价最高的
失败全都是静默的——app 明显在意的某个状态（否则它不会去 Set）渲染器从来
不看，画出来的东西以一种讲得通的方式是错的，而没有任何地方说这件事。
`onSetRenderState`/`onSetSamplerState` 现在把每个不在消费白名单里的 state id
记进 `getStats().unreadStateIds`（只记 id 不记次数，天然有界）。这把"这里
为什么看起来不对"从猜测变成一张有限的清单。上面第 4 条正是用这个思路找到的。

**2026-08-08：Warcraft III 投影阴影黑块修复。** 截图里覆盖大片地形的黑色
三角/梯形不是 shadow texture 本身，而是 `D3DTTFF_PROJECTED` 的齐次除法把
负 `q` 写成了 `max(q, 1e-6)`：投影仪背后的坐标因此变成极大的正 UV，整块
地形三角形反复采到阴影贴图边缘的不透明 texel。fixed-function cascade 与
shader `_dz`/`_dw` modifier 现在都用保留符号的 epsilon 除数。与此同时补齐
War3 阴影 pass 会依赖的 blend factor/separate alpha、双面 stencil/reference、
constant/slope depth bias，并修复同一 render pass 内关闭 scissor 后仍沿用上一
draw 矩形的动态状态泄漏。对应回归同时覆盖 WGSL 文本、pipeline descriptor 与
真实 WebGPU validation。

**2026-08-09：Warcraft III 战役 UI、战争迷雾、阴影与鼠标的共同状态根因。**
进入战役后才同时暴露出的四种表象，并不是四个互不相关的贴图缺陷，而是 guest
状态缓存从错误初值开始：`device_init_states()` 把 render state、texture-stage
state 全部清成 0，sampler state 在 Reset 时甚至没有重置；但三个 `Set*State`
入口都会在“缓存值等于新值”时省略协议命令。D3D9 的真实默认值大量不是 0，
因此第一次显式切换会被错误吞掉：

- UI/文字/图标 pass 常先设 `ZENABLE=FALSE`、`ZWRITEENABLE=FALSE`。旧缓存认为
  它们已经是 FALSE，不发命令；host 按正确的 D3D9 默认值继续开着深度测试与
  深度写入，场景深度便会挡住后画的 glyph/icon quad；
- 迷雾和投影遮罩常让 stage 1 复用 texcoord 0。D3D9 的
  `D3DTSS_TEXCOORDINDEX` 默认值是 stage 自己的编号，因此 stage 1 默认应为 1；
  旧缓存却是 0，War3 第一次写 0 时被省略，host 继续从不存在/错误的 texcoord 1
  取样，表现为整层遮罩错误或消失；
- `COLORARG*`/`ALPHAARG*` 也存在同类非零默认值，错误的首个状态省略会直接改变
  UI glyph alpha 与多层纹理合成；
- shadow/fog overlay 的 `ZWRITEENABLE=FALSE` 被省略后还会污染后续深度，令表象
  随绘制顺序变化，看起来像随机阴影错误。

现在 guest 初始化完整的 D3D9 render/texture-stage/sampler 默认值，Reset 同样
恢复它们；host 的初始 blend factor 也由错误的 `SRCALPHA/INVSRCALPHA` 对齐为
D3D9 的 `ONE/ZERO`。这样“相等则省略”才是合法优化。除此之外，固定管线实现了
`D3DTADDRESS_BORDER` + `D3DSAMP_BORDERCOLOR` 的 shader 级越界替换（WebGPU
sampler 没有 border colour），避免投影坐标落到域外时把边缘不透明 texel 扩散
到整块地形；GDI 指针回退改为默认启用，应用自己设置的 D3D9 hardware cursor
仍有更高优先级，确实自己画鼠标几何的游戏可用 `D9WG_GDI_CURSOR=0` 关闭。

站点同时把 `game.html` 中两个 D3D9 host 脚本的 cache key 从旧的
`d3d9-m3-20260808d` 提升到 `d3d9-m5-war3-state-20260809`。只重新执行
`build.sh` 会更新 guest DLL，但投影除法、BORDER 和 WebGPU pipeline 都在 host
JavaScript 中；不提升 URL 的浏览器缓存可能让新 DLL 继续搭配旧 executor，造成
“已经重编 DLL 但画面完全没变”的假阴性。

对应自动验证为：52 个 executor batch 回归、31 个 shader translator 回归、
113 个由 Naga 实编译的 WGSL 变体，以及真实 Chrome/WebGPU 页面（90 条命令，
5 个 draw，`droppedDraws=0`、`shaderCompileErrors=0`、WebGPU validation PASS）。

### M3.5：真实游戏 shader 语料（新增，前置于任何翻译器改动）

`D9WG_DUMP_SHADERS=1` 让 guest DLL 把每个 shader 的原始 token 流按内容
hash 写到 DLL 旁边的 `d3d9_dump\` 目录（`{vs,ps}_<主><次>_<hash>.d9sh`，
纯 DWORD 流无头部）。`glbridge/tests/d3d9_shader_corpus_test.js` 是它的
离线消费端：喂给 `compileShader` 并（可选地）过一遍 `naga`，按错误信息
分组报告，而不是逐个 shader 列——一个游戏公共 shader 前缀里的一条未实现
指令会命中几十个 shader，平铺列表会把第二个真正不同的问题埋掉。

这件事的价值是复利的，也是这条路径目前最大的缺口：翻译器是手写的，它的
正确性证据只有本仓库自己的测试，而那些测试用的是**我们手工汇编的**
字节码——恰恰是错误的采样，因为手写测试里只有我们已经想到的编码。任何一款
D3D9 游戏都是语料贡献者，哪怕它在 v86 里只能跑到主菜单就卡住：客户端在
启动阶段就会创建完整的 shader 集合，几分钟就能换来一份永久可离线重放的
回归语料。

语料默认**不作为构建门槛**（新导入一款游戏本来就应该暴露未实现指令，为此
让构建失败会让人干脆不再导出语料）。等某份语料被作为基线签入之后，用
`D9_CORPUS_STRICT=1` 打开门槛。

### M4：独立 sampler state 收尾 + MRT + WoW 登录/选角

**2026-08-08：本节的渲染能力清单已完成**（渲染目标主体在 M3 提前完成，
本轮补齐局部操作与精确来源回读）。剩下的是 WoW 客户端本身的人工验收：

- ~~MRT（12 章后半部分）。~~ M3 完成。
- ~~`StretchRect`/`ColorFill`。~~ M3 完成主体；M4 补齐部分矩形
  `ColorFill` 和带 rect list 的颜色/深度/stencil `Clear`。
- ~~`GetRenderTargetData`（对已知来源内容）。~~ guest 为 render target
  懒建 CPU 镜像；完整 `Clear`/`ColorFill` 与同格式等尺寸的已知源
  `StretchRect` 可以精确回读，任何 draw 都会令镜像失效。任意 GPU draw
  结果的同步回读仍按 2.2 节明确不做。
- ~~Occlusion/Event Query（第 13 章）。~~ M3 以 guest 侧保守结果完成；
  真正的 GPU 侧计数需要第 6.7 节的 host→guest 回传通道，仍未做。
- 验收：WoW 登录界面、服务器列表、角色选择（含单个 3D 角色模型预览与
  基础光照）稳定运行。

### M4.5（可选，用于提前拆解风险）：WoW 固定管线/最低画质路径

**2026-08-08：能力协商已实现。** `D9WG_CAPS_PROFILE=ffp`（也接受 `m4.5`/
`low`）把 `VertexShaderVersion`/`PixelShaderVersion` 如实降为 0，保留 M1-M4
已实现的固定管线、贴图与 MRT caps；`sm2`/`m5` 是默认档。旧的
`D9WG_SHADER_MODEL=0` 只作为兼容别名保留。是否存在可用的 WoW 低画质路径
仍需要真实客户端验收。

- 背景与前提条件见第 4.7 节。仅在阶段 0 的 WoW API trace 确认"客户端
  真的存在可用的低 shader 版本降级路径"之后才排入计划；如果确认降级
  路径本身残缺，跳过本里程碑直接进入 M5。
- `GetDeviceCaps` 如实上报当前固定管线能力（不含 SM2.0/3.0），不新增
  D9WG opcode，只是提前用真实 WoW 客户端验证 M1-M4 已实现的固定管线/
  贴图/MRT 基础设施。
- 验收：WoW 在最低画质设置下进入主世界，角色/地形/UI 可见（画质粗糙
  可接受，渲染错误/崩溃不可接受），作为 M5 完整 SM2.0/3.0 路径的提前
  的健全性检查（sanity check），而不是最终交付标准。

### M5：WoW 主世界渲染

**2026-08-08：代码侧主世界渲染清单已完成，真实 WoW 验收待办。**

- 蒙皮声明新增 `UBYTE4`/`SHORT2`/`SHORT4`/`UDEC3`/`DEC3N`，与已有的
  `BLENDWEIGHT`/`BLENDINDICES` 语义、shader 常量寄存器和 dirty-range 骨骼
  调色板上传合起来覆盖该路径。
- M3 已完成的多级 fixed-function texture cascade、cube texture 和 SM2
  translator 覆盖地形多层混合与环境反射；本轮补齐 sRGB render-target 写入。
- 完整接入 blend factor/separate alpha、双面 stencil、stencil reference、
  constant/slope depth bias，并修复 render-pass 内 scissor 动态状态泄漏。
- 自动验证为 31 项 shader pipeline、112 个 Naga WGSL、50 项 executor；
  真实 Chrome/WebGPU 页同时覆盖局部 Clear/ColorFill、UBYTE4 shader 输入、
  cube/RTT 和 sRGB back-buffer view，GPU validation 为 PASS。

- 顶点蒙皮（`BLENDWEIGHT`/`BLENDINDICES` 声明 + vertex shader 里的
  骨骼矩阵调色板，矩阵调色板通过 `SetVertexShaderConstantF` 大批量更新，
  这是对第 9.7 节 uniform 打包/dynamic offset 设计的真正压力测试）。
- 地形多层混合、环境/立方体贴图反射。
- 验收：进入游戏世界后可以移动镜头、看到地形与角色正确渲染，UI（小地图、
  聊天框、单位框）正确。

### M6：特效、后处理与性能收敛

- 粒子系统（通常是大量小 `Draw*UP` 或点精灵，复用已有路径，重点是
  批处理效率）。
- 后处理通道（如果实际观测到，例如热浪扭曲等全屏效果，通常表现为
  "渲染到贴图再采样"的标准 MRT/RTT 模式，不需要新协议能力）。
- Shader 编译 Worker 化（9.5 节的 pending 机制）、持久编译缓存（19 章）、
  vs_3_0 顶点纹理采样（若需要，9.9 节）、instancing（若需要，
  `SetStreamSourceFreq`）。
- 性能预算收敛（第 16 章）。

**2026-08-09：M6 host 核心已实现，进入目标游戏人工验收阶段。**

- `D3DPT_POINTLIST` 已转换为单次 instanced triangle-list：每个源点生成 6 个
  quad 顶点，支持固定管线 `D3DRS_POINTSIZE`、每顶点 `PSIZE`、min/max、
  A/B/C 距离衰减和 `POINTSPRITEENABLE` 自动 UV；可编程 VS 的 `oPts` 同样
  进入这条扩展路径。带索引的 point list 会先按 CPU shadow 压成连续实例流。
- 已有 MRT/RTT/`StretchRect` 路径继续承担后处理，不增加只为某个效果存在的
  协议命令；back-buffer 抓取与异格式 blit 仍由真实 WebGPU 浏览器测试覆盖。
- 基础 shader 字节码翻译在专用 Worker 中执行；Worker 不可用或运行失败时
  明确计数并回退到主线程。翻译结果通过 IndexedDB 跨会话保存，按 LRU
  顺序限制为 2 MiB；持久化内容是 WGSL/反射信息，不是 `GPUShaderModule`。
- 常量上传改为每设备一块 16 MiB persistent uniform ring + dynamic offset，
  并增加有界 bind-group cache。相同 draw 常量在一帧内复用槽位；只有 ring
  溢出才创建临时 uniform buffer，且会进入 `uniformRingOverflows`。
- `getStats()` 新增 shader cache hit/miss、p50/p95/p99、缓存 WGSL 字节数、
  Worker/持久化计数、bind-group 命中/淘汰、uniform 复用/溢出、每帧创建/
  submit/pass 数、点精灵数量和 MRT attachment 分布。M4 的 occlusion query
  仍是 guest 端 conservative-visible 策略，因此明确报告 mode 与 0 个 host
  slot，避免把“没有 host slot 池”误判成统计失效。
- `d3d9_webgpu_perf_test.js` 的 150 draw 稳态负载在 2 次 warm-up 后达到：
  **0 pipeline、0 bind-group、0 buffer 创建，1 render pass、1 submit、无回读**，
  满足第 16.1 节预算。当前自动回归为 33 项 shader pipeline、57 项 executor、
  115 个 Naga WGSL；真实 Chrome/WebGPU 页含点精灵在内为 PASS。

`vs_3_0` 顶点纹理采样和 `SetStreamSourceFreq` 仍按“若真实客户端需要才实现”
处理，当前 caps 不宣称支持。M6 只有在真实 XP/v86 目标游戏中完成至少一个
粒子密集场景和一个后处理场景的截图/指标验收后，才可标记为最终完成；上述
状态不替代第 16.3 节的人工验收门槛。

每个里程碑结束时，未达标功能不得通过"假装支持"糊弄过关：延续 D3D8 路径
"未实现返回 `D3DERR_INVALIDCALL`、caps 诚实"的纪律。

## 16. 性能设计与预算

### 16.1 每帧目标预算（相对 D3D8 路径的调整）

D3D8 路径的目标是"每帧最多三次 PCI 提交、零 pipeline 创建、无 GPU
回读"。D3D9 路径尤其是 WoW 的 draw call 量级更高，预算相应放宽但仍需
明确上限，避免"能跑但很卡"被当作交付：

- 稳态每帧 PCI 提交次数：≤ 5（比 D3D8 路径宽松，因为 MRT/多 pass 特效
  可能需要多次提交，但仍需是常数级而非随 draw call 数增长）。
- 稳态每帧 pipeline/bind-group 创建次数：0（缓存命中率是关键指标，
  首次遇到新 shader/新状态组合除外）。
- 稳态无 GPU 回读（第 2.2 节原则）。
- **新增**：首次编译 shader 的延迟需要独立采集（第 16.2 节），不与
  稳态帧时间混在一起统计，否则会掩盖"加载时卡顿"这一真实存在的问题。

### 16.2 必须暴露的运行时统计（在 D3D8 路径统计项基础上新增）

- shader 编译缓存命中/未命中次数，未命中时的编译耗时分布（p50/p95/p99）。
- 已缓存 shader 总数、总 WGSL 源码大小（估算内存/持久化存储占用）。
- occlusion query 槽位使用率与"槽位耗尽退化为总是可见"的触发次数。
- MRT 场景下的 attachment 数量分布。

### 16.3 性能对照方式

与 D3D8 路径相同：真实 XP + WineD3D 链路的基线数据（阶段 0 采集）与
D9WG 路径逐里程碑对比，任何里程碑验收前必须有一次真实 Chrome + v86
XP guest + 目标游戏的手工验收，不能只依赖 host 端单元测试通过就宣布
达标。

## 17. 与 D3D8 路径共享代码的重构建议

优先级从高到低（第 3.1 节已列出候选模块，这里给出建议顺序，避免在
D3D9 路径尚不稳定时就强行重构 D3D8 路径引入不必要的回归风险）：

1. **M1 期间**：新建 `glbridge/webgpu-runtime/webgpu_device.js`
   （设备获取/丢失监听/canvas 生命周期）与 `lru_cache.js`（通用有界
   LRU），`d3d9_executor.js` 从一开始就依赖它们；`d3d8_executor.js`
   **暂不**改造，避免同时改两条已知能跑的路径和一条新路径。
2. **M2 完成后**（D9WG 传输层与编译缓存设计已经过至少一款游戏验证）：
   评估把 `d3d8_executor.js` 里对应的内联实现迁移到共享模块，用 D3D8
   的现有测试套件（`d3d8_webgpu_executor_test.js` 等）作为回归防线。
3. **M3 完成后**：评估 `vgl2_transport.h` 头文件抽取（7.2 节），让
   `d3d8_proxy.c` 迁移过去，同样要求 D3D8 现有验收测试全部通过。

不建议把这三步压缩到 D3D9 路径的 M1 阶段一次性做完——那样任何一个环节
出错都无法区分是"D3D9 新代码的问题"还是"重构 D3D8 共享代码引入的问题"。

## 18. 测试体系

### 18.1 协议单元测试

`glbridge/tests/d9wg_protocol_consistency_test.js`：镜像现有
`d3d8_protocol_consistency_test.js`，验证 `d3d9_protocol.h` 里的
`#pragma pack(1)` 结构体大小与 JS 侧解析常量一致，防止头文件改动后
两端 silently 失配。

### 18.2 host 单元测试

`node --test glbridge/tests/d3d9_webgpu_executor_test.js`：覆盖顶点
声明→`GPUVertexBufferLayout` 映射的每种 `D3DDECLTYPE`、sampler state
去重、MRT attachment 组装、occlusion query 槽位分配/回收。

`glbridge/tests/d3d9_shader_pipeline_test.js`：字节码编译管线的单元
测试，覆盖 vs_1_1/2_0/3_0 与 ps_2_0/3_0 各自至少一个含控制流
（`if`/`loop`）的真实着色器样本、常量寄存器打包、编译失败时的
"标记不可用而非崩溃"路径、hash 缓存命中/未命中行为。这组测试需要真实
跑通 vkd3d-shader(wasm) 和 Tint(wasm)，不能只测试"调用了某个 mock
函数"。

`glbridge/tests/d3d9_webgpu_perf_test.js`：镜像现有 D3D8 性能测试，
pin 住第 16.1 节的稳态预算（用计数假设备，不需要真实 GPU）。

### 18.3 浏览器 WebGPU 测试

`glbridge/tests/d3d9_webgpu_browser_test.html`：真实 WebGPU 设备上
创建覆盖每种目标特性的 pipeline（顶点声明变体、MRT、cube/volume
采样、vs_3_0 顶点纹理采样若已实现），断言 `getCompilationInfo()` 无
`error` 级别消息，并渲染出可视化验证画面（延续 D3D8 页面"报告 PASS
才算通过"的方式，不能只看控制台无异常）。

### 18.4 Windows guest 渐进测试

延续 D3D8 路径的分阶段验收可执行文件模式
（`build_stageN_tests.sh` → `/private/tmp/d3d9-stageN-tests`），按
第 15 章的 M1-M6 拆分对应的 stage 编号，每个 stage 覆盖该里程碑新增的
API 面而不是重新跑一遍前面已经覆盖过的用例。

### 18.5 分游戏验收矩阵

三款游戏各自维护一份验收清单（截图/录屏 + 关键指标数字），格式参考
D3D8 路径 README 里 MapleStory 验收段落的写法：具体到"第几关""哪个
场景""预期看到什么""实测数值"。

### 18.6 回归要求

任何后续修改 D9WG 协议或 `d3d9_executor.js` 的改动，必须先跑通已经
达到的最高里程碑对应的 stage 测试和浏览器测试，不能只跑新增功能的
测试就合并。

## 19. Save state、恢复与确定性

### 19.1 图形恢复

与 D3D8 路径相同的原则：guest 影子状态是恢复真相来源，host 端资源表
从 checkpoint 中的 guest 状态重建。新增内容：

- 顶点声明、shader 字节码影子、shader 常量寄存器堆都需要进入
  checkpoint（延续"guest 保真"原则，这些都是 guest 端持有的真相）。
- **shader 编译结果缓存不需要进入 checkpoint**：它是从字节码 hash
  可确定性派生的产物，checkpoint 只需要保存字节码本身（已经作为
  shader 影子的一部分保存），加载时重新查缓存（若持久化缓存命中则
  直接复用，未命中则重新编译，这个重新编译不算"恢复错误"，是正常的
  缓存未命中路径）。

### 19.2 编译结果的跨会话持久缓存

实现使用浏览器 `IndexedDB` 存储 `{bytecode_hash -> translation_result}` 映射
（不持久化 `GPUShaderModule` 本身，它是运行时对象，每次加载都要用
缓存的 WGSL 源码重新 `createShaderModule`）。淘汰策略为 LRU，持久快照上限
2 MiB，schema 版本由 storage key 隔离；IndexedDB 不可用时只退回会话内
内存缓存并增加失败统计。这一项直接影响 WoW 场景下"第二次
打开网页时加载资料片内容是否还要重新跑一遍全部 shader 编译"的体验，
优先级高于大多数 M6 的其他优化项。

### 19.3 音频恢复

不变：延续 D3D8 路径的方案，与图形 save state 完全独立。

## 20. 部署、开关与回退

### 20.1 按游戏配置

延续现有站点的"每个游戏一个独立磁盘镜像/部署 profile"模式（参见
`game/` 目录与 `README.md` 里 `populateGamePage()` 的组织方式）：
KartRider/War3/WoW 各自的部署目录里放各自匹配里程碑进度的 `d3d9.dll`，
**不与** `d3d8.dll`、自定义 `opengl32.dll` 共存于同一目录（第 4.6 节）。

### 20.2 feature flags

复用 D3D8 路径的 feature-bit 协商机制（6.3 节）：host 端可以按浏览器
能力（例如 `texture-compression-bc` 是否可用、`shader-f16` 是否可用）
关闭某些 feature bit，guest 端据此调整 caps 上报，而不是等到运行时
才因为具体某个格式创建失败而崩溃。

### 20.3 构建产物管理

延续现有 `build.sh`/`build_stageN_tests.sh` 模式；额外新增
vkd3d-shader(wasm)、Tint(wasm) 的构建脚本
（`glbridge/d3d9-webgpu/build_shader_toolchain.sh`），产物体积较大
（两个 WASM 编译器），需要评估是否随页面首屏加载还是按需懒加载（建议
懒加载：只有第一次遇到需要编译的 shader 时才拉取，登录界面等纯 UI
阶段可能完全不需要触发这条路径）。

## 21. 风险与应对

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| vkd3d-shader/Tint 裁剪编译到 WASM 的工程量被低估 | M2 延期，阻塞后续所有里程碑 | 阶段 0 结束后立刻做一次独立的"能否把 vkd3d-shader 的 D3D9 前端 + SPIR-V 后端单独编译成 WASM"的技术尖刀验证（spike），只用一个真实 SM2.0 着色器打通全链路，作为 M2 立项前置条件，而不是等到 M2 里程碑内部才发现行不通 |
| WoW 内容持续更新导致 shader 集合事实上无上限 | 编译缓存永远追不上新内容，首次加载卡顿常态化 | 优先做持久化缓存（19.2）与 Worker 化编译（9.5），把"卡顿"限定在真正首次见到某 shader 的那一次，且卡顿是加载期而非帧率下降 |
| D3DX9 运行时依赖范围超出预期（尤其是运行时 HLSL 编译） | 可能需要额外的 HLSL 编译器工程，超出本方案范围 | 阶段 0 必须明确回答（14.5 节的"行动项”），一旦确认存在则单独立项，不混入本方案时间线，也不为了赶时间而"假装支持"运行时编译 |
| 三款游戏可能存在反作弊或联网校验组件 | 无法在离线/私服环境下运行，或运行时被判定异常 | 本方案只覆盖单机/离线/私服场景下的渲染兼容层；若目标客户端版本内置强制联网反作弊，需要用户自行选择兼容的私服或离线可运行版本，这不是渲染层要解决的问题，本方案不做任何反作弊绕过设计 |
| MRT/浮点渲染目标在部分浏览器 WebGPU 实现上支持不完整 | 特定效果在部分用户设备上无法渲染 | feature bit 协商（20.2）降级：不支持时该效果直接跳过或退化为单目标近似，而不是崩溃或黑屏 |
| pipeline key 结构比 D3D8 复杂得多，容易引入缓存未命中或错误复用 | 要么性能不达标（缓存爆炸），要么渲染错误（错误复用了不该复用的 pipeline） | 第 18.2 节的单元测试必须包含"结构不同但哈希容易碰撞"的边界用例；性能测试（18.2）持续 pin 住"pipeline 创建次数为零"的稳态断言 |
| 版权与分发边界模糊 | 法律/合规风险 | 本方案与现有站点一致：不提供、不分发游戏客户端或版权资源，只提供用户自备正版客户端时的渲染兼容层；文档与代码仓库都不应包含任何游戏资源文件 |

## 22. 推荐 PR 拆分

1. `docs`: 本文档 + 架构总览（已完成）。
2. `protocol`: `d3d9_protocol.h` + `d9wg_protocol_consistency_test.js`，
   无渲染行为，只锁定二进制契约。
3. `guest-skeleton`: `d3d9proxy` 骨架（`IDirect3D9`/`IDirect3DDevice9`
   生命周期 + 批处理框架），不含任何绘制。
4. `host-skeleton`: `d3d9_executor.js` 骨架 + `glbridge/webgpu-runtime/`
   共享模块的最初两个文件（17 章第 1 步）。
5. `m1-fixed-function`: 顶点声明 + 无 shader 的固定管线 draw 路径，
   端到端跑通 `d3d8_clear_test.exe` 等价的 D3D9 版本测试。
6. `shader-toolchain-spike`: vkd3d-shader/Tint 的 WASM 构建脚本 +
   一个真实 SM2.0 着色器的端到端编译验证（21 章第一条风险的验证结果），
   建议作为独立 PR 而不是塞进 m2，方便单独 review 构建产物体积与
   许可证声明。
7. `m2-shader-pipeline`: 完整字节码编译管线 + 常量寄存器打包 + 独立
   sampler state，KartRider 达到可玩标准。
8. `m3-warcraft3`: 多层地形混合、状态块扩展、cube/volume（按需）。
9. `m4-mrt-and-wow-login`: MRT、query、WoW 登录/选角验收。
10. `m5-wow-world`: 顶点蒙皮、环境贴图、WoW 主世界验收。
11. `m6-perf-and-effects`: Worker 化编译、持久缓存、粒子/后处理、性能
    收敛。
12. `shared-refactor-*`: 17 章第 2、3 步的 D3D8/D3D9 共享代码重构，
    各自独立 PR，且要求先合并对应里程碑再发起。

## 23. 发布门槛清单（分游戏）

### KartRider

- [ ] 主菜单、角色选择、赛道加载无渲染错误。
- [ ] 至少三条赛道从起点跑到终点，帧率达标（2.3 节）。
- [ ] 水面/反光等 SM2.0 特效视觉正确（人工比对真实 XP+WineD3D 基线截图）。
- [ ] `d3d9_webgpu_perf_test.js` 稳态预算全部通过。
- [ ] 无 `D3DERR_INVALIDCALL` 被静默吞掉导致的黑屏/缺失渲染（日志审计）。

### Warcraft III

- [ ] 单机战役至少三关从过场到目标达成。
- [ ] 地形多层混合、单位选框、小地图 UI 视觉正确。
- [ ] 存档/读档（v86 save state）在战役中途保存后能正确恢复渲染状态。
- [ ] 长时间游玩（≥30 分钟单场战斗）无内存/句柄泄漏（资源计数器审计）。

### World of Warcraft

- [ ] 登录、服务器列表、角色选择界面稳定运行，含 3D 角色预览。
- [ ] 进入主世界后可正常移动镜头，地形/角色/植被渲染正确。
- [ ] UI（小地图、聊天框、单位框、技能栏）渲染正确。
- [ ] 顶点蒙皮角色动画正确（无扭曲/撕裂）。
- [ ] 至少一个包含粒子特效的场景（如法术施放）渲染正确。
- [ ] shader 编译缓存持久化生效：二次访问同一区域加载明显快于首次。
- [ ] `d3d9_webgpu_perf_test.js` 稳态预算全部通过，且首次编译延迟已
      从稳态帧时间统计中分离并单独达标。

### 通用可靠性与回退

- [ ] 三款游戏各自的部署 profile 互不干扰，且都能通过关闭 `d3d9.dll`
      回退到 WineD3D/OpenGL 代理路径验证问题是否出在新路径本身。
- [ ] 设备丢失恢复（WebGPU device lost 模拟）在三款游戏上都能自动
      重建渲染状态而不需要重启游戏进程。

## 24. 当前仓库状态与下一步建议

截至 2026-08-07：

- D3D8 路径（D8WG v1.7）已实现到 M4+/shader-model-1.x 里程碑，是本
  方案在传输层、批处理、缓存设计上的直接参照物。
- `glbridge/d3d9proxy/`（guest DLL，M1 验收达成）与
  `glbridge/d3d9-webgpu/`（`d3d9_executor.js` + M2 新增的
  `d3d9_shader_pipeline.js`）已建立。`glbridge/webgpu-runtime/`
  仍未创建——见 M1 状态记录里"等出现第二个消费者再抽取"的判断，M2 没有
  改变这个结论。
- `game/warcraft3.img` 是本地开发用镜像，按
  `tests/site-configuration.test.js` 的断言刻意**没有**接进站点的游戏
  目录；KartRider、WoW 的镜像/部署流程仍未准备。这意味着 M1/M2 的真实
  游戏验收都只能人工执行，无法进 CI。

**建议的下一步：**

1. 人工跑 M2 的验收：War3 进入实战场景，按 M2 状态记录里列的五项统计
   判读结果（必要时用 `D9WG_CAPS_PROFILE=ffp` 二分；旧的
   `D9WG_SHADER_MODEL=0` 仍兼容）。这是目前唯一
   卡在自动化之外的验证。
2. 顺带在同一次运行里跑 `glbridge/sample/d3d9_shader_test.exe`（已随
   `build.sh --tests` 构建），它比整局游戏更容易定位失败点。
3. 阶段 0（第 5 章）的 API trace 采集仍未做。M2 的翻译器目前是按 D3D9
   规格书实现的，没有真实游戏的 shader 语料做覆盖率参考——这在进入 M5
   （WoW，shader 种类事实上无上限）之前必须补上。
4. 确认 KartRider、WoW 的镜像/资源获取与部署方式。

## 25. 参考资料与许可边界

- Direct3D 9.0c SDK 文档（`D3DVERTEXELEMENT9`、shader model 2.0/3.0
  指令集、caps 位定义）。
- vkd3d-shader（Wine 项目，LGPL-2.1-or-later）：D3D9 字节码解析与
  SPIR-V 生成的参照/裁剪来源。若最终方案编译并分发其代码的 WASM
  产物，必须在产物旁附带 LGPL 许可证文本与源码获取方式说明（LGPL 对
  静态链接到 WASM 模块的分发有明确的源码提供义务，需要在正式引入前
  确认满足方式：动态可替换的 WASM 模块，或提供构建脚本与对应源码
  快照）。**M2 落地时没有引入 vkd3d-shader**（见 9.3 的落地修正），
  所以这项义务目前不适用；如果日后替换后端，需要先解决它。
- Tint（Chromium/Dawn 项目，BSD-3-Clause）：SPIR-V 到 WGSL 转换的
  来源，BSD-3-Clause 要求保留版权声明，义务比 LGPL 轻，但仍需在
  `THIRD_PARTY_NOTICES` 类文件中列出。**同样未引入**——Tint 只在浏览器
  自己的 WebGPU 实现里被用到（`createShaderModule` 背后），那是运行环境
  提供的，不构成分发。
- naga（`wgpu` 项目，MIT/Apache-2.0）：**仅作为开发期校验工具**，通过
  `cargo install naga-cli` 由开发者自行安装，
  `glbridge/tests/d3d9_shader_wgsl_validation_test.js` 在检测不到它时
  跳过。不随本仓库分发，不进入运行时。
- 本文档第 3.1、17 章提到的与 D3D8 路径共享设计范式，参照
  `d3d8-webgpu-architecture.md` 中已经记录的 BottleShip 参考边界
  （BottleShip 是 Apache-2.0，D3D8 路径是基于可观察架构的原创实现；
  D3D9 路径延续同样的立场——参考公开可观察的行为/架构决策，不照抄
  受版权保护的实现代码）。
- [`LostMyCode/d3d9-webgl`](https://github.com/LostMyCode/d3d9-webgl)
  （MIT 许可，用于 GunZ: The Duel 的浏览器移植 Whiplash GunZ）：架构
  模型（源码级重编译到单一 WASM 二进制，面向 WebGL2）与本方案（面向
  未修改二进制、走 v86 + PCI DMA + WebGPU）不同，不作为代码复用来源，
  详见第 3.3 节评估。三处具体技术被本方案采纳并记录在对应章节：
  clip plane 用 fragment shader discard 模拟（9.11）、WebGPU 纹理原点
  与 D3D9 一致而不需要 WebGL 式的渲染目标 Y 翻转（8.4）、以及"caps
  如实反映能力从而让应用自行走向其固定管线降级路径"这一验证过的策略
  被推广为 WoW 的 M4.5 里程碑选项（4.7）。MIT 许可比 LGPL/BSD 更宽松，
  但本方案没有照搬其 GLSL/C++ 源码，只是复用了上述三条思路，仍在此列出
  归属以保持透明。
- 本方案不提供、不分发任何游戏客户端、资源文件或反作弊绕过手段；
  三款目标游戏均需用户自备。
