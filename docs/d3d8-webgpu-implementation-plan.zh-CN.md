# D3D8 → WebGPU 直接转译完整实施方案

> 状态说明：本文保留早期迁移方案及当时的旧链路描述。文中“保留
> OpenGL/gl4es/WebGL 回退”的决定已经作废；当前 OpenGL 只有 WebGPU 后端。

> 项目：`retro-gaming-site`
>
> 首要兼容目标：MapleStory v0.83（Windows XP / v86）
>
> 文档性质：架构与实施计划。本文不表示所有功能已经完成，也不要求立即继续修改代码。
>
> 核心目标：用应用目录内的自定义 `d3d8.dll` 将高层 D3D8 命令批量送到浏览器 WebGPU 后端，绕过 WineD3D、OpenGL proxy、gl4es 与 WebGL。

## 1. 背景与问题定义

当前图形链路是：

```text
MapleStory.exe
  → d3d8.dll / WineD3D 1.7.52
  → OpenGL 调用
  → opengl32 proxy
  → v86 PCI / DMA
  → gl4es
  → WebGL
  → 浏览器 GPU 进程
```

MapleStory v0.83 目前约为 5–6 FPS。问题不只是 WebGL 本身，而是一个 D3D8 状态变化或 draw call 会在链路中被多次拆解、编码、解码和重新组合：

1. WineD3D 把 D3D8 状态转换为多条 OpenGL 状态调用。
2. OpenGL proxy 再把每条 GL 调用编码成跨 guest/host 的命令。
3. 浏览器端解码后，gl4es 再做一次固定管线与 GLES/WebGL 适配。
4. 高频 `SetRenderState`、`SetTextureStageState`、`SetTexture` 等调用产生大量 guest CPU、I/O trap、DMA 提交、JavaScript 解码和状态重复设置开销。

新链路应缩短为：

```text
MapleStory.exe
  → 自定义 d3d8.dll
  → D8WG 高层批处理协议
  → 现有 v86 PCI / DMA 通道
  → D3D8 WebGPU executor
  → WGSL / WebGPU
```

这不是把 gl4es 改造成 WebGPU，也不是在旧 WineD3D 中加入 WebGPU backend，而是建立一条独立的 D3D8 前端和浏览器执行器。

## 2. 目标、非目标与成功标准

### 2.1 项目目标

- 保留完整 Windows XP guest 和现有 v86 执行环境。
- 通过应用目录内的原生 32 位 `d3d8.dll` 实现真正的 D3D8 COM 接口。
- 在 guest 中维护 D3D8 逻辑状态和资源 shadow，在 host 中维护 WebGPU 资源。
- 将一帧内的状态、资源更新和 draw call 合并为少量 DMA batch。
- 在浏览器端实现 D3D8 固定管线到 WGSL/WebGPU 的直接映射。
- 优先打通 MapleStory 使用的 2D/Gr2D 热路径，再扩展完整 D3D8。
- 保留旧 WineD3D → OpenGL → gl4es → WebGL 路径作为按游戏选择的回退后端。
- 后续使用独立的 DirectSound → WebAudio 路径替换 MapleStory 对 SB16 的依赖。

### 2.2 非目标

- 第一阶段不追求一次性完整实现 Direct3D 8 的全部接口。
- 不把 WebGPU API 暴露给 Windows guest。
- 不在普通绘制路径中进行 GPU → CPU readback。
- 不让每个 D3D8 API 调用产生一次 PCI doorbell 或一次 JavaScript 消息。
- 不把 BottleShip 的 PE loader、HLE COM 指针或 guest stack dispatcher 原样搬入完整 XP guest。
- 不使用 WebGPU 输出声音。WebGPU 不提供扬声器输出；声音必须走 Web Audio。
- 在 DirectSound 路径通过验证前，不全局关闭 SB16。

### 2.3 最终成功标准

MapleStory v0.83 达到以下条件才算项目目标完成：

- 进程未加载 WineD3D 版本的 `d3d8.dll`。
- 未加载或调用当前 `opengl32.dll` proxy。
- gl4es 与 WebGL 不参与该游戏的绘制。
- 登录界面、角色选择、地图、人物、怪物、UI、文字、粒子和过场可正确显示。
- 分辨率切换、最小化/恢复、切换地图和长时间运行不出现资源错乱。
- 正常稳定帧的 PCI 图形提交不超过 3 次，目标为 1 次主提交。
- 稳态每帧 WebGPU pipeline 创建数为 0。
- 正常 `Present` 路径没有 GPU readback。
- 图形性能相较旧链路有明确且可复现的提升。
- DirectSound 路径完成后，BGM 和音效连续，持续运行无可感知断音，音频 underrun 为 0。

不能只用“达到 30 FPS”作为唯一完成条件。绕过旧图形桥后，瓶颈可能转移到 v86 的 x86 执行、游戏逻辑、网络、定时器或浏览器主线程，所以必须同时观察分层耗时。

## 3. 总体架构

```text
┌──────────────────────── Windows XP guest / v86 ────────────────────────┐
│                                                                        │
│  MapleStory.exe                                                        │
│       │                                                                │
│       ├── Direct3D 8 ──→ app-local d3d8.dll                            │
│       │                     ├── COM 对象                               │
│       │                     ├── shadow state                           │
│       │                     ├── resource shadow                        │
│       │                     └── D8WG batch encoder                     │
│       │                                                                │
│       └── DirectSound ──→ app-local dsound.dll（后续独立阶段）          │
│                             ├── COM 对象                               │
│                             ├── PCM shadow                             │
│                             └── V8DS control/upload queue              │
│                                                                        │
│               v86gl.sys / 连续 DMA arena / PCI BAR                    │
└───────────────────────────────┬────────────────────────────────────────┘
                                │ guest physical memory
┌──────────────────────────── browser / host ─────────────────────────────┐
│                               │                                        │
│                    protocol dispatcher                                 │
│                      ┌────────┴────────┐                               │
│                      │                 │                               │
│             D3D8 WebGPU executor   DirectSound backend                 │
│              ├── parser             ├── control SAB                    │
│              ├── state              ├── PCM heap SAB                   │
│              ├── resources          └── AudioWorklet mixer             │
│              ├── WGSL/pipelines             │                          │
│              └── WebGPU                     └── Web Audio              │
│                    │                               │                    │
│                  Canvas                         speaker                 │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.1 BottleShip 中可参考的设计

[BottleShip](https://github.com/jenissimo/bottleship) 的运行方式与本项目不同：它通过 PE loader、导入表 patch、Win32/D3D HLE 和 v86 CPU 执行程序，并不运行一套完整 Windows XP。适合参考的是：

- D3D8 shadow state 模型。
- guest 资源与 host GPU 资源分离。
- D3D 固定管线的 WGSL 表达。
- immutable pipeline key。
- pipeline、bind group 和 sampler cache。
- 高频 setter 的快速路径与批处理原则。
- 按 guest 顺序提交绘制。
- DirectSound/waveOut 与 Web Audio 分离的音频架构。

以下部分必须针对本项目重写：

- PE import patch 和 HLE 调用截获。
- HLE COM 对象地址模型。
- guest stack 参数读取与 thunk dispatcher。
- BottleShip 自身的 Win32 窗口集成。
- 与其 flat-memory runtime 绑定的代码。

本项目的截获点应是一个真正运行在 Windows XP 内、遵守 D3D8 ABI 的 32 位 COM DLL。

### 3.2 组件职责边界

| 组件 | 负责 | 不负责 |
|---|---|---|
| `d3d8.dll` | COM 语义、参数检查、逻辑状态、资源 shadow、批处理 | 创建 WebGPU 对象、WGSL 编译 |
| D8WG protocol | 稳定、可验证的 guest/host 二进制契约 | 表达 JavaScript 对象或 guest 指针 |
| v86 PCI/DMA | 搬运、提交、同步状态、错误码 | 理解完整 D3D8 状态 |
| WebGPU executor | 资源表、状态应用、pipeline/cache、命令编码 | 模拟 Windows COM 生命周期 |
| `dsound.dll` | DirectSound COM、缓冲区语义、播放控制 | 图形提交、最终音频混音 |
| AudioWorklet | 实时取样、重采样、混音、cursor | 等待 `Present()` 或同步 GPU |

## 4. 核心架构决策

### 4.1 第一版复用现有 PCI 与驱动 ABI

现有 XP 驱动、连续 DMA 内存、BAR doorbell、guest RAM 读取和状态返回已经能够承担命令搬运。第一版不应同时重写这些基础设施。

建议保留外层 VGL2 descriptor，在其中放入一个完整 D8WG batch：

```text
V86GLDMADesc
  command_count = 1
  command_stream:
    [VGL2 function = 0xFFE0][extended payload size]
      D8WGBatchHeader
      D8WGCommandHeader + payload + padding
      D8WGCommandHeader + payload + padding
      upload payloads
```

这样可以保留：

- XP 驱动与 BAR ABI。
- DMA arena 管理方式。
- v86 save-state 对 PCI 设备的既有处理。
- 一次提交读取完整连续 guest RAM 的能力。
- 旧 OpenGL 协议与新 D8WG 协议并存的能力。

等图形和音频都稳定后，再决定是否把 `v86gl` 泛化命名为 `v86media`。重命名不是性能关键路径，不应阻挡第一版。

### 4.2 guest 保存逻辑真相，host 保存 GPU 真相

guest 侧保存：

- 当前 render/texture-stage/sampler/transform/light 等逻辑状态。
- COM 引用计数和对象关系。
- `MANAGED`、`SYSTEMMEM` 及兼容性需要的资源 shadow。
- Lock/Unlock、dirty range、pool、usage、format、level 等 D3D8 语义。

host 侧保存：

- `GPUBuffer`、`GPUTexture`、`GPUTextureView`、`GPUSampler`。
- shader module、render pipeline、bind group 和缓存。
- WebGPU canvas/context、depth buffer 和 frame encoder。

两端通过带 generation 的整数 handle 关联，不共享对象指针。

### 4.3 图形按帧批处理，音频按实时队列处理

图形的正常 flush 条件：

- `Present`。
- DMA arena 即将用满。
- 同步查询或 readback。
- 资源销毁与仍在 batch 中的引用发生冲突。
- `Reset`、显式 fence 或不可避免的同步操作。

音频不能依赖 `Present`。其 `Play`、`Stop`、`SetCurrentPosition` 和流式 `Unlock` 应立即提交或进行 1–5 ms 微批处理。

### 4.4 回退必须在启动游戏前决定

后端选择建议为：

```text
graphicsBackend = d3d8-webgpu | wined3d-webgl
audioBackend    = directsound-webaudio | sb16
```

浏览器在启动虚拟机或启动游戏前完成：

1. 检查安全上下文与 WebGPU 能力。
2. 请求 adapter/device。
3. 检查必要 texture format feature。
4. 配置 canvas。
5. 若选择直接音频，确保 AudioContext 可在用户手势中恢复。
6. 根据结果部署对应 guest DLL 和配置。

不要尝试在同一 Windows 进程中间从自定义 `d3d8.dll` 热切换回 WineD3D；COM 对象、资源和 device 状态无法安全迁移。

## 5. 阶段 0：先建立可复现基线

在继续扩展实现前，先回答“5–6 FPS 到底耗在哪里”。没有基线就无法确认新路径是否解决了正确问题。

### 5.1 固定测试环境

记录以下信息：

- git commit 与所有本地 patch。
- 浏览器名称、完整版本、启动参数。
- macOS/Windows/Linux 版本。
- CPU、GPU、内存。
- v86 构建版本、WASM 优化选项和内存配置。
- 游戏客户端 hash、地图、角色、分辨率和窗口模式。
- 是否开启浏览器 DevTools、性能采样或日志。
- 是否启用音频、网络和后台标签页节流。

建议固定三段场景，每段至少采集 60 秒：

1. 登录界面或角色选择。
2. 低负载地图中静止。
3. 同一地图持续移动、释放技能并出现多个实体。

每个场景至少重复 3 次，记录中位数和 P95，而不是只看瞬时 FPS。

### 5.2 旧链路必须采集的指标

| 层级 | 指标 |
|---|---|
| 游戏/D3D8 | API calls/frame、draws/frame、状态 setter/frame、纹理/VB Lock 次数 |
| WineD3D/GL | GL calls/frame、shader/program 切换、纹理上传字节数 |
| PCI | submits/frame、bytes/frame、doorbell 次数、同步等待次数 |
| host bridge | 解码 ms/frame、临时分配次数、复制字节数 |
| gl4es/WebGL | 状态转换 ms、shader 编译数、draw 数 |
| WebGPU 新路径 | command 数、pipeline/bind-group 创建数、encode/submit 时间 |
| v86 | worker CPU、主线程 CPU、WASM 执行时间、事件循环长任务 |
| 用户体验 | FPS median/P95、frame-time P95/P99、卡顿次数、声音 underrun |

### 5.3 API 使用面追踪

在实现完整接口前，必须追踪 MapleStory 实际调用：

- 所有 `IDirect3D8`、`IDirect3DDevice8` 与 resource 方法的次数。
- 所有 FVF、primitive type、render state、texture-stage state 和 sampler state 的取值集合。
- 所有创建过的 format、pool、usage、尺寸和 mip level。
- 是否使用 state block、additional swap chain、render target、depth stencil、readback。
- `SetVertexShader` 传入的是 FVF 还是真实 shader handle。
- 是否创建 vertex/pixel shader 1.x。
- 是否使用 `D3DPT_TRIANGLEFAN`。
- 是否存在跨线程调用 D3D8。
- 音频导入与运行时加载：`dsound.dll`、`winmm.dll`、DirectMusic、FMOD、BASS 等。

日志应支持采样或聚合计数，避免日志本身把 5 FPS 再降一个数量级。

### 5.4 阶段 0 退出条件

- 有一份可重复执行的基准操作说明。
- 旧链路三类场景的性能表已保存。
- MapleStory 使用到的 D3D8 状态/格式/接口集合已生成。
- 已确认音频 API 的真实使用情况。
- 能将总帧时间粗分为 guest CPU、图形桥、host 编码和 GPU 执行。

## 6. D8WG 二进制协议设计

协议是 guest DLL 与 host executor 之间最难修改的公共边界。应先写规范和 parser 测试，再继续增加功能。

### 6.1 基本规则

- 所有整数均为 little-endian。
- batch header 固定大小；command header 固定大小。
- command 总长度包含 header，并按 8 字节对齐。
- 所有 offset 都相对于当前 batch 起点或 upload 区起点。
- 禁止在协议中传 guest virtual pointer 或 JavaScript 对象 ID。
- 所有资源使用 32 位 generation handle。
- host 在执行任何命令前先验证整个 batch 的边界和结构。
- 未识别 opcode 根据版本策略返回明确错误，不能静默忽略。
- 协议错误只能终止当前 batch，不能越界读取 guest RAM。

### 6.2 推荐 batch header

```c
#define D8WG_MAGIC 0x47573844u /* "D8WG" */

typedef struct D8WGBatchHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t frame_id;
    uint32_t flags;
    uint32_t command_count;
    uint32_t command_bytes;
    uint32_t upload_offset;
    uint32_t upload_bytes;
} D8WGBatchHeader;

typedef struct D8WGCommandHeader {
    uint16_t opcode;
    uint16_t flags;
    uint32_t size;
    uint32_t sequence;
    uint32_t reserved;
} D8WGCommandHeader;
```

上面是协议草案，最终字段大小必须通过 guest/host 共用的 golden binary test 固定，避免 C padding 与 JavaScript 解析偏移不一致。

### 6.3 版本与 feature negotiation

`HELLO` 至少交换：

- 协议 major/minor。
- guest DLL build ID。
- host executor build ID。
- 最大 batch 大小。
- 最大 resource handle 数。
- 支持的 opcode feature bits。
- adapter limits 和必要 WebGPU features。
- 支持的 D3D format 位图。

策略：

- major 不一致：拒绝创建设备。
- guest minor 高于 host：只有被 feature bit 明确协商的命令才可使用。
- host minor 高于 guest：保持向后兼容。
- 未协商的能力不得通过 `GetDeviceCaps` 宣称支持。

### 6.4 资源 handle

推荐 32 位布局：

```text
bits  0–19: resource table index
bits 20–31: generation
```

要求：

- handle 0 表示 null。
- host table entry 保存 type、generation、object、metadata 和 last-use serial。
- 释放时 generation 增加，防止旧命令误用新资源。
- handle type 不匹配时返回 protocol error。
- generation 回绕需要跳过仍可能出现的历史值或扩大 generation 位数。

### 6.5 opcode 分组

| 分组 | 第一批 opcode |
|---|---|
| 会话 | `HELLO`、`CREATE_DEVICE`、`RESET`、`FENCE` |
| 帧 | `BEGIN_SCENE`、`END_SCENE`、`CLEAR`、`PRESENT` |
| Buffer | `CREATE_BUFFER`、`UPDATE_BUFFER`、`DESTROY_RESOURCE` |
| Texture | `CREATE_TEXTURE`、`UPDATE_TEXTURE`、`GENERATE_MIPS` |
| Binding | `SET_STREAM_SOURCE`、`SET_INDICES`、`SET_TEXTURE` |
| State | `SET_RENDER_STATE`、`SET_TSS`、`SET_SAMPLER_STATE`、`SET_VIEWPORT`、`SET_SCISSOR` |
| Fixed function | `SET_FVF`、`SET_TRANSFORM`、`SET_MATERIAL`、`SET_LIGHT`、`LIGHT_ENABLE` |
| Draw | `DRAW_PRIMITIVE`、`DRAW_INDEXED`、`DRAW_UP`、`DRAW_INDEXED_UP` |
| Render target | `SET_RENDER_TARGET`、`COPY_RECTS`、`READBACK` |
| Shader | 后续加入 create/set/delete VS/PS 1.x |

### 6.6 上传区

资源更新命令只引用 batch 中的连续 payload：

```c
typedef struct D8WGUpdateBuffer {
    D8WGCommandHeader header;
    uint32_t handle;
    uint32_t destination_offset;
    uint32_t source_offset; /* relative to batch base */
    uint32_t byte_count;
} D8WGUpdateBuffer;
```

上传规则：

- guest encoder 为 payload 保留对齐空间并复制 dirty range。
- host 用 guest WASM memory 的 view 读取，不额外 `slice()`。
- WebGPU `writeBuffer` 的 offset/size 对齐由 host 做安全适配。
- Texture upload 明确记录 mip level、array layer、origin、extent、guest row pitch 和 format。
- 使用 `copyBufferToTexture` 时 staging row pitch 满足 WebGPU 对齐；小型不规则区域可走 `writeTexture`。
- 压缩格式必须按 block 尺寸验证区域和 pitch。

### 6.7 同步命令与状态页

默认异步。以下操作才允许 fence 或同步返回：

- `CreateDevice` / `Reset` 的最终结果。
- 必须返回 host 数据的 capability query。
- `GetRenderTargetData`、lockable render target、front-buffer capture 等 readback。
- WebGPU device lost 或 host fatal error。
- 调试模式下的显式 fence。

状态页至少保存：

- last submitted/completed sequence。
- last frame ID。
- last error code、opcode 与 sequence。
- device epoch。
- bytes/commands/draws/uploads/presents 计数。
- device-lost reason code。

普通 `Set*`、`Draw*`、`Clear` 不得各自同步等待。

### 6.8 parser 安全要求

解析流程必须先验证、后执行：

1. 验证 descriptor 与 batch 的物理地址和总长度。
2. 验证 magic、版本、header size。
3. 验证 `command_bytes + upload_bytes` 没有整数溢出。
4. 首次遍历所有 command，验证 size、对齐、opcode 最小长度和 payload offset。
5. 验证 command count 与实际遍历一致。
6. 验证所有 resource handle、format、enum 和 range。
7. 全部通过后再执行命令。

需要为 parser 建立 malformed corpus：零长度 command、超长 command、整数回绕、未知 opcode、越界 upload、错误 handle type、重复 destroy、错误纹理 pitch 等。

## 7. Windows XP guest：自定义 `d3d8.dll`

### 7.1 建议目录结构

```text
glbridge/d3d8proxy/
  README.md
  build.sh
  d3d8.def
  d3d8_protocol.h
  d3d8_main.c
  d3d8_interfaces.c
  d3d8_device.c
  d3d8_resources.c
  d3d8_state.c
  d3d8_commands.c
  d3d8_formats.c
  d3d8_trace.c
```

早期可以少文件实现，但进入 Maple 兼容阶段前应按职责拆分，避免一个 COM vtable 文件同时承担协议、资源和状态逻辑。

### 7.2 构建与 ABI

- 目标为 32 位 x86 PE DLL。
- Windows subsystem 最低版本保持 XP 兼容。
- 调用约定、vtable 顺序、结构 packing 与 D3D8 SDK 完全一致。
- 尽量沿用现有 MinGW/no-CRT 方案，避免引入 XP 不支持的 CRT 依赖。
- `.def` 至少正确导出 `Direct3DCreate8`。
- 用 `objdump`/PE parser 验证 imports、exports、subsystem version 和重定位。
- 用专门 ABI 测试检查每个 vtable slot，而不只检查能否启动。

### 7.3 COM 对象实现顺序

第一组：

- `IDirect3D8`
- `IDirect3DDevice8`
- `IDirect3DVertexBuffer8`
- `IDirect3DIndexBuffer8`
- `IDirect3DBaseTexture8`
- `IDirect3DTexture8`
- `IDirect3DSurface8`

第二组：

- `IDirect3DSwapChain8`
- `IDirect3DCubeTexture8`
- `IDirect3DVolumeTexture8`
- `IDirect3DVolume8`

每个对象至少保存：

- vtable 指针。
- 原子或受锁保护的引用计数。
- object type。
- parent device 引用。
- host resource handle。
- D3D 创建参数与资源描述。
- shadow memory、dirty regions 与 lock 状态。

### 7.4 COM 语义

实现时必须逐项验证：

- `QueryInterface` 支持正确 IID，失败返回 `E_NOINTERFACE`。
- 成功 `QueryInterface` 必须 `AddRef`。
- child resource 对 device 的引用关系与 D3D8 行为一致。
- `GetDevice`、`GetContainer` 返回正确对象并增加引用。
- `Release` 到 0 时先处理未提交 dirty 数据，再排队销毁 host handle。
- 不能因为某方法暂未实现就返回成功并丢弃绘制工作；应返回准确的 `D3DERR_INVALIDCALL` 或未支持错误。
- 对 Maple 会探测但不实际使用的接口，可以返回保守 caps 或明确失败。

### 7.5 capabilities 策略

`GetDeviceCaps` 不能复制一张理想显卡的能力表。应只报告当前 host 和 executor 已完整支持的交集。

实施顺序：

1. 根据阶段列出支持矩阵。
2. 对 MapleStory 实际读取的 caps 做日志。
3. 每增加一项 advertised cap，都新增对应测试。
4. 对 WebGPU adapter limits 做上限裁剪。
5. 对无法原生支持但已有可靠 CPU/shader fallback 的格式或状态，可报告支持。

“为了让游戏继续启动而虚报能力”会把启动时的清晰错误变成绘制过程中的随机错误，应避免。

### 7.6 shadow state 与重复设置抑制

device 维护完整逻辑状态：

- render state 数组。
- texture stage state 数组。
- sampler state 数组。
- texture bindings。
- stream source、stride、index buffer和 base vertex index。
- FVF 或 shader handle。
- world/view/projection 与 texture transforms。
- viewport、scissor。
- material、lights、clip planes。
- render target 与 depth-stencil surface。

普通 setter 的处理：

```text
参数验证
  → 与 shadow 比较
  → 相同：直接返回 D3D_OK
  → 不同：更新 shadow，并将最小状态变化编码进 batch
```

例外：

- `BeginStateBlock` 期间要记录调用语义，不能简单丢掉重复 setter。
- `CaptureStateBlock` 必须捕获逻辑状态。
- `ApplyStateBlock` 应比较当前状态后批量发出最小差异。
- `Get*` 从 guest shadow 返回，不应同步询问 host。

### 7.7 多线程策略

MapleStory 即使主要单线程，也不能假设所有 D3D8 调用永远来自一个线程。

- 若 `D3DCREATE_MULTITHREADED` 未设置，可保持轻量路径，但在 debug 中检测跨线程误用。
- 若设置，device、resource table、batch encoder 和 refcount 必须受同一明确锁顺序保护。
- 禁止在持有 device lock 时进行可能无限等待的 host fence。
- `Lock` 返回的 shadow memory 生命周期必须跨越调用线程。

### 7.8 资源 pool 与 Lock/Unlock

| Pool/Usage | guest 策略 | host 策略 |
|---|---|---|
| `SYSTEMMEM` | 完整 shadow，是权威副本 | 需要绘制/复制时上传 |
| `MANAGED` | 完整 shadow，记录 dirty | host 可丢弃并从 shadow 恢复 |
| `DEFAULT` 静态 | 保留必要 metadata；初期可保留 shadow | WebGPU resource 是主要副本 |
| `DEFAULT + DYNAMIC` | upload ring + dirty range | buffer renaming，避免等待 GPU |

`D3DLOCK_DISCARD`：

- 不等待旧 slice。
- 分配新的 upload-ring slice 或新的 host buffer generation。
- 后续 draw 使用新 binding。

`D3DLOCK_NOOVERWRITE`：

- 允许写入本轮尚未被 GPU 使用的尾部。
- debug 模式检测与 in-flight range 重叠。

普通 `Unlock`：

- 只发送实际 dirty range。
- Texture 只发送 dirty rect/dirty mip。
- 静态资源内容未变化时不得重复上传。

## 8. 浏览器端 WebGPU executor

### 8.1 建议模块

```text
glbridge/d3d8-webgpu/
  d3d8_executor.js
  d3d8_protocol.js
  d3d8_device.js
  d3d8_resources.js
  d3d8_state.js
  d3d8_formats.js
  d3d8_vertex_layout.js
  d3d8_ffp_shader.js
  d3d8_pipeline_cache.js
  d3d8_bind_group_cache.js
  d3d8_sampler_cache.js
  d3d8_upload.js
  d3d8_metrics.js
```

这表示最终职责划分，不要求一次性重构到该布局。

### 8.2 初始化生命周期

1. 在用户选择游戏后检测 `navigator.gpu`。
2. `requestAdapter`，记录 adapter info 和 limits。
3. 根据需要请求 `texture-compression-bc` 等 feature；请求失败时启用 CPU fallback。
4. `requestDevice` 并安装 `device.lost` handler。
5. 获取独立 WebGPU canvas/context，配置 preferred format 与 alpha mode。
6. 创建基础 uniform ring、dummy texture/sampler 和默认 depth surface。
7. 完成 D8WG `HELLO` 后才允许 guest `CreateDevice` 成功。

若 v86 和 guest memory 位于 worker，executor 最好也位于同一 worker 并使用 `OffscreenCanvas`，避免每个 batch 再经 `postMessage`。如果当前产品结构暂时要求在主线程执行，应先测量复制与消息成本，再决定迁移，不要盲目重构。

### 8.3 每个 batch 的执行流程

```text
validate batch
  → apply resource creates/updates/destroys
  → update logical host state
  → on Clear/Draw: begin or reuse render pass
  → resolve shader variant and pipeline key
  → fetch/create pipeline
  → update uniform ring and bindings
  → encode draw
  → on incompatible target/state: close pass and begin another
  → on Present: close pass, finish encoder, queue.submit, present
  → write completion/error metrics
```

不得在每个 `Set*` 时立即创建 pipeline。只有 draw 时才根据当前完整状态解析 pipeline。

### 8.4 render pass 管理

- 第一次 `Clear` 或 draw 时按需创建 pass。
- 只改变 viewport、scissor、blend constant、stencil reference 时不重开 pass。
- render target/depth target 改变时结束当前 pass。
- `Clear` 若可用 pass loadOp 表达则合并；局部 clear 或中途 clear 需要专用 draw/新 pass。
- `Present` 强制结束所有 pass 并提交。
- frame 内没有绘制也要正确处理 `Present` 与 canvas acquisition。

### 8.5 device lost

WebGPU device lost 与 D3D8 的 Reset/lost-device 语义不同，需要显式桥接：

- host 增加 `device_epoch`。
- device lost 时停止执行新 draw，状态页记录原因。
- guest 后续调用返回与 D3D8 兼容的错误，触发游戏的 reset 路径。
- 新 device 创建后，所有 host handle 旧 generation 失效。
- `MANAGED`/shadowed 资源从 guest shadow 重传。
- pipeline、bind-group、sampler、texture view cache 全部重建。
- 恢复完成前不得让旧 batch 在新 device 上执行。

## 9. D3D8 固定管线到 WebGPU

MapleStory 首个可玩版本应优先覆盖 2D fixed-function 热路径，而不是从灯光或 shader model 1.x 开始。

### 9.1 FVF 与 vertex layout

优先实现：

- `D3DFVF_XYZRHW`
- `D3DFVF_XYZ`
- `D3DFVF_NORMAL`
- `D3DFVF_DIFFUSE`
- `D3DFVF_SPECULAR`
- `D3DFVF_TEX1`
- `D3DFVF_TEX2`
- 每级 texture coordinate size 修饰位。

解析器必须：

- 从 FVF 计算精确 stride 和每个 attribute offset。
- 校验传给 `SetStreamSource` 的 stride。
- 将 D3D color `DWORD` 的 BGRA byte order 正确解释为 shader 输入。
- 区分 `SetVertexShader(FVF)` 与真实 vertex shader handle。
- 对 indexed draw 正确处理 base vertex index、min index 和 num vertices。

### 9.2 `XYZRHW` 预变换路径

这是 Maple 2D 绘制的关键：

- x/y 已位于 D3D viewport 空间，不再乘 world/view/projection。
- z 需保持 D3D 的 `[0, 1]` 深度约定。
- rhw 用于透视正确插值；不能当作普通 w 无条件忽略。
- 明确处理 D3D8 与 WebGPU 的 Y 方向和 viewport 映射。
- 通过像素中心 golden test 确认半像素偏移，不能只凭经验固定加减 `0.5`。
- scissor 与 viewport 需要在同一坐标约定下验证。

### 9.3 primitive topology

直接映射：

- point list。
- line list。
- line strip。
- triangle list。
- triangle strip。

WebGPU 没有 triangle fan。`D3DPT_TRIANGLEFAN` 必须转为 indexed triangle list：

```text
输入顶点：0, 1, 2, 3, 4
生成索引：0,1,2  0,2,3  0,3,4
```

UP draw 可直接在 transient upload ring 中生成；持久 VB 的 fan 可使用临时 index ring，避免改写原 buffer。

### 9.4 第一版 shader 策略

第一版使用覆盖 Maple 常用状态的“小型 uber shader”：

- stage 0/1 color op 和 alpha op 由 uniform 选择。
- alpha reference、texture factor、fog 参数等通过 uniform 传递。
- shader module 数量保持很小，降低冷启动和 pipeline explosion 风险。

初期 texture-stage op：

- `DISABLE`
- `SELECTARG1`
- `SELECTARG2`
- `MODULATE`
- `MODULATE2X`
- `MODULATE4X`
- `ADD`
- `ADDSIGNED`
- `SUBTRACT`
- `BLENDTEXTUREALPHA`
- `BLENDDIFFUSEALPHA`

初期 argument：

- `CURRENT`
- `DIFFUSE`
- `SPECULAR`
- `TEXTURE`
- `TFACTOR`
- `COMPLEMENT`
- `ALPHAREPLICATE`

Maple 路径稳定后，可根据 trace 中的高频组合生成 specialized WGSL，但必须保留 shader key 上限和回退 uber shader。

### 9.5 render state 映射

| D3D8 状态 | WebGPU/WGSL 实现 |
|---|---|
| Z enable/write/func | depthStencil state |
| alpha blend | color target blend state |
| src/dst blend | WebGPU blend factors；不支持组合用 shader fallback |
| alpha test | fragment WGSL `discard` |
| cull mode | primitive state |
| fill mode | WebGPU 原生能力不足时限制 caps 或 debug fallback |
| shade mode | 需要 flat/smooth 插值 variant；先覆盖 trace 中实际值 |
| color write | color target write mask |
| stencil | depthStencil stencil front/back |
| fog | vertex/pixel WGSL 路径 |
| lighting | fixed-function vertex WGSL |
| texture factor | uniform |
| blend factor | dynamic blend constant（适用时） |

重要原则：影响 pipeline descriptor 的状态进入 pipeline key；只改变数值的状态放入 uniform 或 WebGPU dynamic state。

### 9.6 pipeline key

必须包含：

- primitive topology、strip index format。
- vertex buffer layout/FVF。
- vertex/fragment shader variant。
- color target format。
- depth-stencil format。
- blend state与 color write mask。
- cull mode、front face。
- depth compare/write。
- stencil configuration。
- sample count。

不得包含：

- world/view/projection 矩阵。
- viewport/scissor。
- texture/buffer handle。
- material、light 和 fog 数值。
- texture factor、alpha reference。
- sampler 或 uniform 的具体对象 handle（它们属于 binding cache）。

Key 应使用结构化整数或稳定 bit packing，不能在热路径频繁构造长字符串。

### 9.7 cache 设计

- Pipeline cache：按完整 immutable pipeline key。
- Shader module cache：按 WGSL variant。
- Sampler cache：按 filter/address/LOD/anisotropy/comparison。
- Texture view cache：按 texture handle、mip、layer、aspect。
- Bind-group cache：按 layout、texture views、samplers、buffer identity；动态 uniform offset 不进入对象 identity。
- Triangle-fan index cache：按 vertex count，可共享常用尺寸。

cache 必须有计数、命中率和上限。不要无界增长；场景切换和长时间运行时应能按 last-use serial 回收。

## 10. Texture、Surface 与格式

### 10.1 第一批格式

| D3D8 格式 | 首选 WebGPU 表达 | fallback |
|---|---|---|
| `A8R8G8B8` | `bgra8unorm` | CPU swizzle 到 `rgba8unorm` |
| `X8R8G8B8` | `bgra8unorm`，上传时强制 A=255 | CPU 转换 |
| `R5G6B5` | 无直接采样格式 | CPU 扩展到 RGBA8 |
| `A1R5G5B5` | 无直接采样格式 | CPU 扩展到 RGBA8 |
| `A4R4G4B4` | 无直接采样格式 | CPU 扩展到 RGBA8 |
| `A8` | `r8unorm` 或 shader swizzle | RGBA8 |
| `L8` | `r8unorm` + shader 扩展 | RGBA8 |
| `DXT1` | `bc1-rgba-unorm` | CPU decode |
| `DXT3` | `bc2-rgba-unorm` | CPU decode |
| `DXT5` | `bc3-rgba-unorm` | CPU decode |
| `D16` | `depth16unorm`（能力允许） | `depth24plus` |
| `D24S8` | `depth24plus-stencil8` | caps 降级 |

最终是否需要 `P8`、`A8P8`、bump map 和更多 depth format，以阶段 0 trace 为准。

### 10.2 Surface 关系

- Texture level surface 与 parent texture 共享同一个 host texture handle，并保存 mip level。
- `GetSurfaceLevel` 返回新的 COM 引用，不重复创建 GPUTexture。
- Render target surface 创建对应 view，并进入 render-pass attachment key。
- Depth-stencil surface 不能作为普通 color texture 使用。
- `CopyRects` 优先用 `copyTextureToTexture`；格式转换时走 compute/render/CPU fallback。
- `UpdateTexture` 根据 dirty mip/rect 批量上传。

### 10.3 mipmap

- 应用提供全部 mip：逐级上传。
- 自动生成 mip：用 WebGPU render/compute mip generator；不能依赖不存在的自动 API。
- 压缩纹理 fallback 解码后，mip 仍按各级尺寸独立处理。
- 1×N、N×1 和非 2 次幂纹理需要测试。

### 10.4 禁止隐式 readback

以下行为不得因为实现方便而出现在正常帧：

- 为 `Get*` 状态从 GPU 读回。
- 为 managed texture 在每帧回读 host 内容。
- 为格式转换把 GPU texture 读回 CPU。
- 为截图功能长期保留每帧 staging copy。

只有 D3D8 明确要求读取 render target/front buffer 时才创建 staging buffer 并 fence。

## 11. 分阶段实施流程

每个阶段都应单独形成可回退的 PR。某一阶段退出条件未通过时，不进入下一阶段的大规模功能开发。

### 阶段 0：基线、API trace 与协议冻结

工作项：

- 建立旧链路性能基线。
- 记录 MapleStory 的实际 D3D8 与音频 API 使用面。
- 写 D8WG v1 字段、opcode、错误码、对齐和版本规范。
- 建立 guest C struct 与 JavaScript parser 的 golden binary fixtures。
- 定义运行时指标格式和调试开关。

退出条件：

- 第 5 节全部完成。
- malformed batch 测试不越界、不崩溃。
- 协议 v1 文档评审完成。

### 阶段 1：传输与最小 Clear/Present

工作项：

- `Direct3DCreate8`、adapter/mode/caps 最小集合。
- `CreateDevice`、`BeginScene`、`EndScene`、`Clear`、`Present`。
- D8WG batch encoder 与外层 VGL2 `0xFFE0` 封装。
- bridge 协议分流与独立 WebGPU canvas/context。
- 错误状态页、frame ID 和基础 counters。
- 旧图形后端配置回退。

测试：

- `d3d8_clear_test.exe`。
- 连续 resize/present。
- 无 draw 的 present。
- 无 WebGPU、adapter 失败和 device lost 注入。

退出条件：

- Clear 颜色和 viewport 正确。
- 每帧只有一个主要提交。
- 旧 WebGL canvas 不被 WebGPU context 冲突破坏。
- 失败时启动前回退或给出明确错误。

### 阶段 2：基础几何与 buffer

工作项：

- Vertex/index buffer 创建、Lock/Unlock、dirty upload、销毁。
- stream 0、indices、FVF。
- `DrawPrimitive`、`DrawIndexedPrimitive`。
- `DrawPrimitiveUP`、`DrawIndexedPrimitiveUP`。
- point/line/triangle list/strip 与 triangle-fan 转换。
- `XYZRHW | DIFFUSE` shader 路径。

测试：

- triangle。
- indexed draw。
- UP draw。
- triangle fan。
- 不同 stride、offset、base vertex。
- buffer 销毁后 stale handle。

退出条件：

- 基础几何 pixel golden 全部通过。
- 重复 `SetStreamSource`/FVF 被 guest shadow 抑制。
- draw 热路径无同步 PCI round-trip。

### 阶段 3：Maple 2D/Gr2D 首个可玩目标

工作项：

- Texture/Surface 与 `LockRect`/`UnlockRect`。
- `A8R8G8B8`、`X8R8G8B8` 和常见 16 位格式。
- FVF `XYZRHW`、`DIFFUSE`、`SPECULAR`、`TEX1/TEX2`。
- texture stage 0/1 的常用 color/alpha op。
- alpha test、alpha blend、texture factor。
- point/linear filter、clamp/wrap。
- viewport 与 scissor。
- 动态 VB/IB 的 `DISCARD`/`NOOVERWRITE`。
- `Draw*UP` transient ring。
- DXT1/3/5 原生或 CPU fallback。

测试：

- texture format tests。
- alpha blend/alpha test。
- multitexture。
- dynamic resource。
- scissor/viewport。
- `d3d8_maple_gr2d_test.exe`。
- MapleStory 登录、角色选择和一个固定地图。

退出条件：

- Maple Gr2D 测试无明显像素差异。
- MapleStory 能进入地图且 UI/精灵/文字/透明边缘正确。
- 首次完成旧路径与新路径的相同场景 A/B benchmark。
- 稳态 pipeline creation 为 0/frame。
- 图形提交目标 1/frame，上限 3/frame。

这是第一次应该正式评估 MapleStory FPS 的阶段。在此之前的 clear/triangle FPS 不代表真实收益。

### 阶段 4：完整 fixed function

完成状态（D8WG v1.4，2026-08-01）：

- 已实现 Maple 黑屏实际触发的 `FVF 0x142`（`XYZ | DIFFUSE | TEX1`），并保留 `XYZRHW` 路径。
- 已实现 world/view/projection、texture transform、depth/stencil、material、directional/point/spot light、specular、normalize normals、local viewer、fog、texture coordinate generation、flat/Gouraud 和 blend op。
- 已增加 transform/depth、raster/stencil、lighting、fog、textured-cube 的 XP 回归构建脚本，以及 host fake-WebGPU 和真实 WebGPU validation 页面覆盖。
- XP 下 Stage 3/4 测试程序已经由人工验收，MapleStory v83 已进入游戏并确认画面正常；阶段 4 的 Maple fixed-function 发布门槛已关闭。
- explicit render target/depth surface 与 `CopyRects` 已在阶段 5 实现。Maple trace 未要求的 clip plane、cube/volume 路径仍返回明确错误，并保持 caps 不宣告，因此不阻塞阶段 4 完成。

工作项：

- `XYZ`、world/view/projection。
- depth/stencil 完整状态。
- material、directional/point/spot lights。
- vertex/texture fog。
- specular、normalize normals、local viewer。
- 更多 texture-stage op 与 texture coordinate generation/transform。
- mipmap、cube texture、volume texture（按 trace 优先级）。
- render target、depth surface、`CopyRects`。
- clip planes 与更完整的 blend/cull/shade 状态。

测试：

- transform/depth。
- lighting/material。
- fog。
- mipmap。
- render-to-texture。
- cube/volume texture（若实现）。

退出条件：

- trace 中 Maple 使用的 fixed-function 状态全部支持。
- caps 不虚报。
- 不常用状态不会造成无界 pipeline variant。

### 阶段 5：生命周期与兼容性

当前落地状态（D8WG v1.6，2026-08-02）：

- 已实现 recorded state block 和 `D3DSBT_ALL`/`PIXELSTATE`/`VERTEXSTATE`，覆盖 begin/end/create/capture/apply/delete；捕获的纹理、VB、IB 保持 COM 引用并在覆盖/删除时释放。
- `Reset` 每次分配全新的 device epoch 和资源 handle namespace；DEFAULT-pool、锁定资源、未结束 scene/state-block、additional swap chain 及隐式 back/depth surface 会按 D3D8 约束阻止 Reset。
- MANAGED/SYSTEMMEM 资源保留 CPU shadow，Reset 后用新 handle 重建并重新上传；当前绑定和默认状态被重置，viewport、尺寸、窗口跟踪和自动 depth 配置同步更新。
- host 保存 retired device/resource handle 集，来自旧 epoch 的延迟 batch 在执行任何资源访问前丢弃；测试覆盖旧 device 的 update/bind/draw 不得触碰新 device。
- WebGPU `device.lost` 会保存 canonical CPU checkpoint、申请/注入 replacement device、清空属于旧 GPUDevice 的 pipeline/sampler 缓存并重建所有设备状态与资源。
- 已实现 additional swap chain 的 COM 生命周期、backbuffer、Present window 路由和 Reset blocker；浏览器仍只有一个 D3D8 canvas，因此它提供兼容的单显示面路由，不模拟多 canvas 同时显示。
- 已实现 render target/depth/image surface、lockable render target、`CopyRects`、`GetFrontBuffer`、`Set/GetRenderTarget`、`GetDepthStencilSurface`，以及 clear/copy/upload 路径的 CPU readback shadow。
- `QueryInterface`、`GetDevice`、Surface `GetContainer`、parent/refcount、失败输出置空、跨 device 对象拒绝和常用尺寸/格式/锁参数边界已经收紧。
- v86 save state 现在在 GL journal 后附带 canonical D3D8 checkpoint；load state 会先等待冷启动 WebGPU executor 取得并配置 `GPUCanvasContext`，再清理当前 GPU namespace、恢复保存时的 device/resource/state epoch，且只有初始化和回放全部成功后才释放排队的 guest 命令。初始化失败不会提前清除现有 D3D8 session。
- 每个 XP D3D8 进程现在生成独立的 64 位 session cookie，并由每个 batch header 和 `HELLO` 双重声明；host 的 device/resource/retired-handle 表按 session 隔离。不同 EXE 即使从完全相同的数值 handle 起步，也不会互相销毁、Reset 或隐藏对方的 canvas；多 session checkpoint 会分记录保存和恢复。
- WebGPU 没有同步 texture mapping。当前 readback 对 CPU upload、`Clear` 和 `CopyRects` 是精确的；仅由任意 GPU draw 产生的像素不会同步回写 guest shadow。Maple 不使用该冷路径；依赖它的其他游戏需要另行实现可暂停 guest 的异步 GPU readback 协议。

工作项：

- State block create/begin/end/capture/apply/delete。
- `Reset`、设备丢失、窗口尺寸变化。
- additional swap chain。
- front buffer/readback/lockable render target。
- resource parent/container/refcount 边界。
- save/load state 的 epoch 与资源重建。
- 错误码和边界参数与 Windows D3D8 行为对齐。

测试：

- state block。
- repeated reset。
- device-lost 注入。
- save/load state 后继续绘制。
- 创建/销毁压力测试。

退出条件：

- 连续运行和多次地图切换无 GPU 资源增长。
- Reset 后画面、资源和逻辑状态恢复。
- stale batch/handle 不会访问新 device 的资源。

自动验收覆盖：

- `d3d8_stateblock_test.exe`：recorded/all state block 的 capture/apply/delete 和实际绘制。
- `d3d8_reset_lifecycle_test.exe`：设备创建/销毁、两次 resize Reset、MANAGED 2D/DXT mip 重建、DEFAULT VB 释放与重建。
- `d3d8_lifecycle_compat_test.exe`：additional swap chain、RT/depth/image/front-buffer、container/refcount、96 次资源循环、8 次 device epoch、绑定资源/设备引用环回收和错误输出。
- `d3d8_caps_audit_test.exe`：核验 Stage 5 对外能力与实现一致；核心纹理、DXT1/3/5 和 render-target texture 必须可用，cube/volume texture、SM1.1 和超过两级纹理在阶段 6 前必须保持未公布并返回正确失败输出。
- host executor：192 次 create/destroy 后 live resource 数不增长，24 次 Reset 后只有一个 device 且零旧资源，72 条旧 epoch 命令被丢弃，并覆盖 device-lost 注入。
- host executor：矩形 color/depth/stencil `Clear` 通过带 scissor 的 GPU clear draw 保持区域语义；被当前 command buffer 引用的 texture/buffer/depth/uniform 在 queue fence 完成后才物理销毁。
- cross-process session：两个客体进程使用完全相同的 device/resource 数值句柄时仍可并存；销毁或延迟重放其中一个会话不会影响另一个，多会话 save/load 后隔离关系保持不变。
- bridge state test：D3D8 checkpoint 字节随 v86 state 保存；覆盖全新 executor 冷恢复、异步初始化失败不破坏 session、初始化期间 guest 命令继续排队，恢复时只重建一次且字节完全一致，旧时间线的排队命令被清除。

发布门槛仍需一次真实 XP/browser 人工运行：四个 Stage 5 exe 标题均为 `PASS`，随后执行 Maple 登录、连续切图、save/load 后继续绘制及 10–30 分钟资源曲线观察。自动测试通过代表实现已落地，不替代这项最终长时间验收。

### 阶段 6：D3D8 shader model 1.x

当前落地状态（D8WG v1.7，2026-08-04）：

- 已实现 `CreateVertexShader`/`CreatePixelShader`/`Delete*`、真实 shader handle 的 `SetVertexShader`（FVF token 仍走固定管线路径）、`SetPixelShader`、`Set/Get{Vertex,Pixel}ShaderConstant`、`Get{Vertex,Pixel}ShaderFunction` 和 `GetVertexShaderDeclaration`（含两段式 size 查询）。
- 新增 opcode `CREATE_VERTEX_SHADER`(0x120)、`CREATE_PIXEL_SHADER`(0x121)、`SET_VERTEX_SHADER`(0x20C)、`SET_PIXEL_SHADER`(0x20D)、`SET_VERTEX_SHADER_CONSTANT`(0x20E)、`SET_PIXEL_SHADER_CONSTANT`(0x20F)，以及 resource kind 4/5；shader 销毁复用 `DESTROY_RESOURCE`。
- shader handle 从与 buffer/texture 不相交的命名空间分配且始终带 bit 0，符合 D3D8「shader handle 与 FVF token 可区分」的约定；handle 在同一 session 内不复用，因此可直接作为 pipeline cache key。
- guest 与 host 各自独立地按同一张支持指令表校验 bytecode。VS 1.1 支持 MOV/ADD/SUB/MUL/MAD/RCP/RSQ/DP3/DP4/MIN/MAX/SLT/SGE/EXP/LOG/EXPP/LOGP/LIT/DST/FRC/DEF/NOP；PS 1.1–1.4 支持上述 ALU 子集加 TEXCOORD/TEX/TEXKILL/LRP/CND(≤1.3)/CMP(≥1.2)/PHASE(1.4)。矩阵宏（`m4x4` 等）和 bump-mapping/`texm3x3` 系列属于合法 D3D8 但不在支持集内，一律明确拒绝而不是近似翻译。
- 已实现 write mask、`_sat`、destination shift（`_x2`/`_x4`/`_x8`/`_d2`…）、source modifier（neg/bias/comp/abs）、任意 swizzle 和 `def` 常量折叠（`def` 覆盖应用设置的常量寄存器）。
- 常量寄存器按 vs 96 / ps 8 组织为单个 uniform buffer，随 `shaderConstantSerial` 缓存；`Reset` 后重新下发 guest shadow，保证重新绑定的 shader 看到相同常量。
- shader pipeline 与固定管线共用同一个 `pipelineCache`，key 以 `"shader"` 前缀加 (vertexShader, pixelShader) handle 对区分命名空间，其余 cull/blend/depth/stencil/color-write 字段与固定管线一致。
- `GetDeviceCaps` 现在如实宣告 `vs_1_1` 与 `ps_1_4`；`MaxVertexShaderConst`/`MaxPixelShaderValue` 沿用先前已预留的值。

工作项：

- Vertex shader declaration parser。
- VS 1.1 指令翻译到 WGSL。
- PS 1.1–1.4 texture/ALU 指令翻译。
- constant register、shader handle 生命周期。
- shader validation、cache 和 fallback error。

退出条件：

- 每条支持指令有独立数值测试。
- 非法 bytecode 被拒绝而不是生成错误 WGSL。
- shader 切换不造成每帧 pipeline 重建。

自动验收覆盖：

- `d3d8_webgpu_executor_test.js`：每条支持指令一条数值断言；write mask/saturate/destination shift/source modifier/swizzle/`def` 折叠；拒绝用例覆盖 vs_1_0、ps_1_5/ps_2_0、版本不匹配的 `cnd`/`cmp`、跨类型 opcode、未声明的 vertex input、越界常量寄存器、超出两级的纹理 stage、不支持的 source modifier、截断指令和截断 `def`；comment token 按长度跳过而非扫描 opcode。
- `d3d8_webgpu_executor_test.js`：端到端真实 shader 绘制，校验生成模块只声明一次 uniform block、vertex layout 来自 declaration 而非 FVF、第二次相同绘制 `pipelineCreations` 不增长，以及未知 shader handle 会让整个 batch 失败。
- `d3d8_protocol_consistency_test.js`：新 opcode/resource kind/结构字段顺序/`sizeof` 断言的 C↔JS 一致性。
- `d3d8_caps_audit_test.exe`（`build.sh --tests`）：要求 caps 如实宣告 SM1.x，执行真实 VS1.1+PS1.1 创建/绑定/绘制/删除，并确认版本错误与不支持 opcode 的 shader 仍被拒绝且句柄清零。

发布门槛仍需一次真实 XP/browser 人工运行：Stage 6 exe 标题为 `PASS`，且在真实 WebGPU 下确认生成的 WGSL 能通过浏览器校验（自动测试使用 fake device，不做 WGSL 编译）。

### 阶段 7：性能硬化

当前落地状态（2026-08-05）：

先按第 5 节的要求建立可复现基线，再动手。基线用一个计数型 fake WebGPU device
测量「150 draw + 300 次 render state 变化」的稳态帧，结果如下（每帧）：

| 指标 | 优化前 | 优化后 | 12.1 预算 |
|---|---:|---:|---|
| GPU buffer 创建 | 150 | **0** | 0 |
| bind group 创建 | 150 | **0** | 接近 0 |
| pipeline 创建 | 0 | 0 | 0 |
| uniform 上传次数 | 150 | **1** | 最少 |
| uniform 上传字节 | 208,800 | **1,392** | 最少 |
| render pass / submit | 1 / 1 | 1 / 1 | 1 |

两处根因与修复：

- **uniform ring + dynamic offset。** 原 `uniformFor` 以单调递增的
  `uniformSerial` 作为 cache key，该 key 永远不会被再次命中，因此每次状态变化
  都会新建一个 `GPUBuffer` 并让 bind group 失效。现在改为一条常驻 uniform ring
  buffer，按 256 字节对齐 suballocate，并用 dynamic offset 绑定；按 §9.7 的要求
  uniform 不再进入 bind group identity，bind group 只由 (ring buffer, textures,
  samplers) 决定。固定管线与 shader 两条路径都改用显式
  `GPUBindGroupLayout`（`hasDynamicOffset: true`），不再使用 `layout: "auto"`。
- **dead-store elimination。** `SET_RENDER_STATE` 过去对任何 render state 都会
  `uniformSerial++`，但 blend/cull/depth/stencil 等属于 pipeline state，并不在
  uniform block 内。现在用一张按 `D3DRENDERSTATETYPE` 索引的 `Uint8Array` 做
  O(1) 判定，只有 uniform block 真正包含的 7 个 render state 才会触发重新打包；
  同时对「写入值与当前值相同」的冗余写入直接丢弃并计数。

其他已落地项：

- uniform 打包改为复用 scratch `Float32Array`，去掉了每次打包的临时数组、
  展开运算符和 `d3dColor()` 临时对象；`setBindGroup` 的 dynamic offset 复用一个
  共享 `Uint32Array`，热路径零分配。
- 新增计数器：`bindGroupCreations`、`bindGroupHits`、`uniformSlotReuses`、
  `uniformUploadBytes`、`uniformRingOverflows`、`redundantStateWrites`。
  `uniformRingOverflows` 持续为 0 表示 ring 容量对当前负载足够。
- ring 与显式 layout 都属于当前 `GPUDevice`，device lost 时与
  pipeline/sampler cache 一并作废重建。

工作项：

- guest state change 合并和 dead-store elimination。
- upload ring、uniform ring 和 transient index ring。
- 避免 JavaScript 临时对象、数组与 `slice()`。
- cache key 整数化与命中率统计。
- pipeline 预热或异步创建策略。
- render pass 合并。
- 大小上传分流：`writeBuffer`/`writeTexture` 与 staging copy。
- cache 上限和长时间回收。
- worker/main-thread 调度与 GC profile。

退出条件：

- Maple 稳态 pipeline creation = 0/frame。
- 正常帧无 readback。
- 提交数和上传字节数满足预算。
- 10–30 分钟运行无持续内存增长和周期性 GC 卡顿。
- 性能数据证明瓶颈已经离开旧图形转换链。

自动验收覆盖（`glbridge/tests/d3d8_webgpu_perf_test.js`）：

- 稳态帧断言 pipeline / bind group / buffer / texture 创建均为 0，render pass
  与 queue submit 均为 1，`copyTextureToBuffer` 与 `mapAsync` 均为 0（正常帧无
  readback）。
- 断言 blend factor 抖动不会重打包 uniform，而 `D3DRS_TEXTUREFACTOR`（uniform
  block 真正包含的状态）必须重打包——这条用于防止 dead-store elimination 优化
  过头导致渲染读到过期 uniform。
- 第二个场景让 uniform 每个 draw 都变化，断言 bind group 缓存规模不随 uniform
  状态增长（≤4），证明 uniform 确实没有进入 bind group identity；仅靠「每帧创建
  数」无法捕获这类回归，因为 ring 游标每帧重置会让 offset 重复出现。
- 断言所有 dynamic offset 均为 256 的整数倍。
- 上述两项优化都做过反向验证：分别把 dead-store 过滤和 bind-group key 改回旧
  写法，测试确实失败。

尚未完成（需要真实运行环境，无法用 fake device 断言）：

- Maple 实机 A/B 性能数据与分层耗时（第 12.3 节）。
- 10–30 分钟长跑的内存增长与 GC 卡顿曲线。
- worker/main-thread 调度与 `OffscreenCanvas` 迁移评估（第 8.2 节要求先测量再
  决定，不应盲目重构）。
- pipeline 预热策略：当前冷启动仍会在首帧创建 pipeline，稳态为 0。

### 阶段 8：DirectSound → WebAudio

这是独立项目阶段，详细设计见第 13 节。它不应阻挡图形路径首次验证，也不能与图形 batch 绑定。

## 12. 性能设计与预算

### 12.1 每帧目标预算

| 指标 | 目标 | 说明 |
|---|---:|---|
| 图形 PCI submit | 1，最多 3 | 主 batch + 极少数容量/同步提交 |
| pipeline creation | 稳态 0 | 冷启动允许 |
| bind-group creation | 接近 0 | 高频组合应命中 cache |
| GPU readback | 0 | 普通帧严格禁止 |
| 重复 state command | 接近 0 | guest shadow 抑制 |
| 完整静态纹理重传 | 0 | 只传 dirty rect/mip |
| JS 中间 buffer copy | 0 或最少 | 直接 view guest memory |
| device/queue submit | 通常 1/frame | pass 合并后提交 |

host decode 和 encode 的具体毫秒预算应在阶段 0 基线后确定。没有测量前不要人为承诺绝对数值。

### 12.2 必须暴露的运行时统计

建议在 debug overlay 或结构化日志中显示：

- FPS、frame time median/P95/P99。
- D8WG batches、commands、draws/frame。
- buffer/texture upload bytes/frame。
- PCI bytes 和 submits/frame。
- parser/decode、WebGPU encode、queue submit CPU 时间。
- pipeline/shader/bind-group/sampler cache size 与 hit rate。
- pass count/frame。
- active buffers/textures/surfaces 与估算 GPU 内存。
- protocol errors、stale handles、device-lost count。
- 音频 voices、upload bytes/s、underrun 与 worklet load。

### 12.3 性能对照方式

每次里程碑使用相同 game save、地图、移动路径和采样时长，对比：

```text
旧：WineD3D + GL proxy + gl4es + WebGL
新：custom d3d8.dll + D8WG + WebGPU
```

同时至少再做两项诊断对照：

- 新后端关闭音频，判断音频线程是否干扰。
- 新后端使用空 draw/真实 draw，判断 guest 游戏逻辑与图形成本比例。

## 13. DirectSound → WebAudio 独立实施方案

### 13.1 正确链路

D3D8 不包含声音，WebGPU 也不能直接把 PCM 输出到扬声器。目标链路应为：

```text
MapleStory.exe
  → 自定义 dsound.dll
  → 独立 DirectSound control/upload 协议
  → SharedArrayBuffer PCM heap + control table
  → AudioWorklet mixer
  → Web Audio output
```

### 13.2 为什么不能跟随图形帧

当前 5 FPS 意味着相邻 `Present` 可能间隔约 200 ms。若音频数据随图形 batch 提交，会产生明显断音。

建议逻辑队列：

```text
Queue 0: D3D8 graphics batch
Queue 1: DirectSound control commands
Queue 2: DirectSound PCM uploads
Status : play/write cursor, notifications, underrun, epoch
```

图形队列按帧；音频 control/PCM 按 1–5 ms 微批或关键命令立即 flush。

### 13.3 实现前先确认 API

检查 MapleStory 主程序及其 DLL 的 imports 和运行时 `LoadLibrary/GetProcAddress`：

- `DirectSoundCreate` / `DirectSoundCreate8`。
- `waveOut*`。
- DirectMusic。
- FMOD、BASS 或其他中间件。
- MIDI/MCI/CD Audio。

只有在确认实际 API 后才能确定 `dsound.dll` 是否足够。若游戏仍使用 `waveOut`，还需要单独的 `winmm.dll` 路径。

### 13.4 guest `dsound.dll`

主要对象：

- `IDirectSound8`
- `IDirectSoundBuffer`
- `IDirectSoundBuffer8`
- `IDirectSoundNotify`

Maple MVP 方法：

- 创建设备、设置 cooperative level、查询 caps/speaker config。
- 创建/复制 primary 与 secondary buffer。
- `Lock`/`Unlock`。
- `Play`/`Stop`。
- current position、volume、pan、frequency、status。
- looping、restore。

每个 secondary buffer 保存完整 guest PCM shadow、format、bytes、cursor、volume/pan/frequency、playing/looping/lost 和 generation handle。

### 13.5 PCM 与控制共享区

建议：

- 一个 `SharedArrayBuffer` PCM heap。
- 一个 `SharedArrayBuffer` control table。
- 固定上限 voice slots，例如 128 或 256。
- 每个 slot 含 generation，避免复用后读到旧声音。

slot 至少记录：

- state/generation。
- PCM heap offset 与 buffer bytes。
- channels、sample rate、bits per sample。
- play cursor、建议 write cursor。
- fractional phase。
- volume、pan、frequency ratio。
- loop flag。
- total played frames。
- underrun 和 notification sequence。

### 13.6 AudioWorklet mixer

每次 render quantum：

1. 清零输出声道。
2. 遍历 playing voices。
3. 读取 PCM。
4. 8-bit unsigned/16-bit signed 转 Float32。
5. mono/stereo 转换。
6. 按 source rate 与 `SetFrequency` 计算 phase step。
7. 线性插值重采样。
8. 应用 DirectSound volume/pan。
9. 累加并 clip。
10. 更新权威 play cursor 与 total played frames。
11. 检测 notification crossing。

必须使用当前 `outputs[0][0].length`，不能假定 AudioWorklet quantum 永远固定。

### 13.7 cursor 与 notification

- play cursor 的权威来源是 AudioWorklet 实际消费的样本数，不是 guest 的 `GetTickCount`。
- write cursor 初期可返回 play cursor 前方 30–60 ms 的安全位置，并根据实际 streaming 行为校准。
- Worklet 用 `Atomics.store` 更新 cursor/sequence。
- guest 查询从 status page 读取最近值。
- `IDirectSoundNotify` 需要由 cursor crossing 触发 guest event；可通过 PCI IRQ 或驱动/helper thread 实现。

### 13.8 浏览器前置条件

- 在用户点击“开始游戏”的手势中创建或恢复 `AudioContext`。
- SharedArrayBuffer 需要 cross-origin isolation。
- 服务端至少正确配置 COOP/COEP，并保证 wasm、worklet、镜像及跨域资源满足 CORP/CORS。
- tab 隐藏、AudioContext suspend/resume 和系统输出设备变化都需要测试。

### 13.9 SB16 迁移策略

第一步保留 SB16，由游戏配置选择新后端。只有以下条件全部满足后，才对 MapleStory 设置 `disable_speaker`：

- 静态音效、循环 BGM 和流式音频测试通过。
- 游戏登录与地图 BGM 正常。
- `GetCurrentPosition` 行为满足游戏。
- 运行至少 10 分钟 underrun 为 0。
- 已确认 Maple 没有仍需 SB16 的 `waveOut`/MIDI/DirectMusic 路径。

不要因为 Maple 的 DirectSound 成功，就对整个 retro gaming site 全局关闭 SB16；DOS、Win9x、MIDI 和其他游戏仍可能依赖它。

## 14. 测试体系

### 14.1 协议单元测试

- C encoder 的输出与固定二进制 fixture 一致。
- JavaScript parser 解析同一 fixture。
- 所有 enum、结构 offset、padding 和大小都有断言。
- fuzz/malformed corpus 不越界、不挂死。
- sequence、generation、unknown opcode 和版本不匹配行为明确。

### 14.2 host 单元测试

使用 fake WebGPU device 或命令记录器验证：

- 状态到 pipeline descriptor 的映射。
- pipeline key 包含/排除字段正确。
- 相同状态命中 cache。
- texture format/pitch/dirty rect 转换。
- triangle fan 索引生成。
- alpha test/TSS shader 计算。
- resource destroy 和 stale handle。

### 14.3 浏览器 WebGPU 测试

- 真实 adapter/device/context。
- headless 浏览器和可见浏览器各一套。
- pixel golden 允许按平台设很小容差，但 alpha/depth/scissor 边界应精确。
- device lost、resize、tab background/foreground。
- Chrome/Edge 为首要；Safari/Firefox 支持范围按产品目标决定。

### 14.4 Windows guest 渐进测试

建议顺序：

1. caps/create device。
2. clear/present。
3. XYZRHW diffuse triangle。
4. indexed/UP/fan。
5. texture/format/LockRect。
6. alpha test/blend。
7. multitexture stage 0/1。
8. viewport/scissor。
9. dynamic resources。
10. transform/depth。
11. lighting/fog/material。
12. render target/mipmap。
13. state block/reset/device lost。
14. shader model 1.x。

每个测试程序必须显示可观察结果，同时把 HRESULT 和 counters 输出到日志，避免“画面空白但进程退出 0”被误判为通过。

### 14.5 MapleStory 验收矩阵

| 场景 | 图形检查 | 性能/稳定性检查 |
|---|---|---|
| 启动/logo | 清屏、尺寸、present | 首次 shader/pipeline 数量 |
| 登录 | UI、文字、透明混合 | frame-time、音频启动 |
| 角色选择 | 精灵层级、alpha、scissor | 上传峰值 |
| 进入地图 | 背景、多层贴图、人物 | pipeline cache 是否稳定 |
| 行走/跳跃 | 纹理坐标、动画 | P95/P99 frame-time |
| 技能/怪物 | 粒子、blend、动态资源 | draw/upload 峰值 |
| 切图 | 资源销毁/创建 | GPU 内存与 cache 回收 |
| 最小化/恢复 | Reset/resize | device lost 恢复 |
| 长时间运行 | 视觉和音频 | 内存增长、GC、underrun |

### 14.6 回归要求

- 旧 `wined3d-webgl` 路径仍能启动其原有测试。
- 新 executor 未启用时不创建 WebGPU canvas/device。
- D8WG batch 不得误交给 GL decoder。
- D8WG `PRESENT` 不得让旧 GL canvas 同时 swap。
- 选择 SB16 的其他游戏不受 DirectSound backend 影响。

## 15. Save state、恢复与确定性

WebGPU 对象和 AudioWorklet 状态不能直接序列化进 v86 save state。

### 15.1 图形恢复

保存：

- guest COM/state/resource shadow 本来就在 guest RAM 内，随快照保存。
- host 只保存轻量 epoch/统计，不尝试序列化 GPU 对象。

加载：

1. 暂停新 D8WG 执行。
2. 增加 host device/session epoch。
3. 清空全部 GPU resource/cache。
4. guest DLL 检测 epoch 变化或收到 restore 通知。
5. 重建 device 与所有需要的 host resources。
6. 从 managed/shadow data 重传内容。
7. 恢复 bindings/state 后继续 present。

### 15.2 音频恢复

- 加载快照前暂停并清空 AudioWorklet voices。
- 增加 audio epoch。
- 从 guest PCM shadow 重建 buffer。
- 恢复 cursor 的策略需要明确：从快照位置继续，或停止后由游戏重新 Play。
- 禁止旧 worklet slot 在新 session 中继续发声。

## 16. 部署、开关与回退

### 16.1 按游戏配置

建议不要覆盖全局 DLL。每个游戏配置显式选择：

```json
{
  "graphicsBackend": "d3d8-webgpu",
  "audioBackend": "directsound-webaudio"
}
```

旧游戏保留：

```json
{
  "graphicsBackend": "wined3d-webgl",
  "audioBackend": "sb16"
}
```

### 16.2 feature flags

开发阶段至少提供：

- D8WG parser strict mode。
- protocol trace sampling。
- state shadow validation。
- pipeline key dump。
- CPU texture conversion 强制开关。
- BC compression 原生/fallback 开关。
- uber/specialized shader 开关。
- cache disable 对照开关。
- debug error scopes。

生产环境默认关闭高频日志和 WebGPU validation-heavy 路径。

### 16.3 构建产物管理

- `d3d8.dll` 与 `dsound.dll` 记录协议版本和 build ID。
- 浏览器 asset 带内容 hash/cache bust。
- guest DLL、host executor 和 protocol header 必须在同一发布清单中。
- host 应拒绝不兼容 DLL，而不是尝试猜测结构。
- 保留上一稳定版本以便快速回滚。

## 17. 风险与应对

| 风险 | 表现 | 应对 |
|---|---|---|
| Maple 使用未追踪接口 | 启动后黑屏或 HRESULT 分支异常 | 阶段 0 聚合 trace，caps 保守 |
| COM ABI/vtable 错误 | 随机崩溃、栈损坏 | vtable slot/调用约定自动测试 |
| batch 过细 | FPS 提升有限 | setter 抑制、帧批处理、提交计数硬指标 |
| pipeline explosion | 周期性卡顿 | 小型 uber shader、稳定 key、cache 上限 |
| WebGPU 格式不匹配 | 贴图颜色/alpha 错 | 明确格式表和 CPU fallback |
| `XYZRHW` 像素约定错误 | 精灵模糊、边缘抖动 | 半像素/viewport/scissor golden tests |
| dynamic buffer 覆盖 in-flight 数据 | 闪烁、随机几何 | ring + serial + DISCARD/NOOVERWRITE 测试 |
| 资源异步销毁竞态 | 换图后随机贴图 | generation handle + last-use serial |
| device lost | 永久黑屏 | epoch、guest shadow、完整重建流程 |
| JS GC | 固定间隔卡顿 | typed-array view、对象池、热路径零分配 |
| 音频跟图形同步 | 低 FPS 时断音 | 独立队列和 AudioWorklet 时钟 |
| SAB 不可用 | DirectSound backend 无法启动 | COOP/COEP 检查与 SB16 fallback |
| 直接复制 BottleShip 代码 | 许可/架构耦合风险 | 优先复用思想；复制时保留 Apache-2.0 notices |
| 新图形链仍非主瓶颈 | FPS 未达预期 | 分层 profile，继续优化 v86/主线程而非盲目扩展 D3D |

## 18. 推荐 PR 拆分

1. **PR 0：基准与 trace**
   - 只加测量、API 聚合日志、基准说明，不改变默认渲染结果。
2. **PR 1：D8WG protocol 与 parser**
   - 规范、golden fixtures、malformed tests、transport 分流。
3. **PR 2：D3D8 Clear/Present**
   - factory/device、WebGPU context、clear/present、fallback。
4. **PR 3：Geometry**
   - VB/IB、Lock/Unlock、FVF、Draw/UP/fan。
5. **PR 4：Maple Gr2D**
   - texture、stage 0/1、alpha、scissor、dynamic resources。
6. **PR 5：Formats and surfaces**
   - 16-bit/DXT、mips、surface relationships、copy paths。
7. **PR 6：Full fixed function**
   - transforms、depth/stencil、lighting、fog、material。
8. **PR 7：Lifecycle**
   - state blocks、reset、device lost、save-state recovery。
9. **PR 8：Performance hardening**
   - rings、cache、pass merging、allocation removal、A/B report。
10. **PR 9：DirectSound static playback**
    - `dsound.dll`、SAB、AudioWorklet、Lock/Unlock、Play/Stop。
11. **PR 10：DirectSound streaming**
    - cursor、loop、frequency、notification、save-state。
12. **PR 11：Maple production enablement**
    - per-game defaults、telemetry、failure fallback、release checklist。

每个 PR 必须满足：

- 默认后端行为可控，不意外影响其他游戏。
- 新增接口有测试。
- 新增 pipeline/resource 路径有 counters。
- 协议变更同步更新 guest header、host parser 与 fixtures。
- 文档更新支持矩阵和已知限制。

## 19. MapleStory 发布门槛清单

### 图形正确性

- [ ] 启动、登录、角色选择和进入地图均无黑屏。
- [ ] `XYZRHW`、纹理坐标与像素中心正确。
- [ ] 透明边缘、alpha test 和常用 blend 正确。
- [ ] UI clipping/scissor 正确。
- [ ] 文字、精灵、多层背景、粒子和技能效果正确。
- [ ] 动态 VB/IB 无闪烁或数据覆盖。
- [ ] 切图、最小化、resize、Reset 后恢复。

### 图形性能

- [ ] WineD3D、OpenGL proxy、gl4es 未参与新路径。
- [ ] 正常帧 PCI submits ≤ 3。
- [ ] 稳态 pipeline creation = 0/frame。
- [ ] 正常帧 GPU readback = 0。
- [ ] cache 数量稳定且有上限。
- [ ] 10–30 分钟无持续 GPU/JS/guest 内存增长。
- [ ] 同场景 A/B 数据确认新路径显著优于旧路径。

### 音频

- [ ] 已确认 Maple 实际音频 API。
- [ ] 静态音效、循环 BGM、volume/pan/frequency 正常。
- [ ] `GetCurrentPosition` 由 AudioWorklet 消费位置驱动。
- [ ] 图形低 FPS 或短暂停顿时音频不中断。
- [ ] 10 分钟 underrun = 0。
- [ ] 直接音频通过后才对 Maple 禁用 SB16。

### 可靠性与回退

- [ ] WebGPU 不可用时在启动前明确回退或报错。
- [ ] device lost 能恢复或给出可操作错误。
- [ ] save/load state 不引用旧 GPU/audio handle。
- [ ] 旧 WebGL/SB16 游戏不受影响。
- [ ] guest DLL 与 host executor 协议版本严格匹配。

## 20. 当前仓库状态与下一步建议

截至 2026-08-04，阶段 4 已由 XP 测试程序和 MapleStory v83 实机验收关闭，仓库协议已推进到 D8WG v1.7：阶段 5 的 lifecycle/compatibility 与阶段 6 的 shader model 1.x 代码和自动回归均已落地。阶段 3/4 的 texture、Gr2D 和 fixed-function 能力继续保留；v1.5/v1.6 新增：

- 完整 state block 生命周期和资源引用捕获；
- 新 device epoch 的 `Reset`、DEFAULT/lock/surface blocker、MANAGED 资源重建和窗口尺寸更新；
- additional swap chain、render/depth/image surface、render-to-texture、`CopyRects`、lockable RT 和 front-buffer CPU mirror；
- device-lost 自动恢复、旧 GPUDevice cache 失效、retired handle/stale batch 隔离；
- canonical D3D8 save/load checkpoint，与 v86 PCI state journal 原子保存和有序恢复；
- COM interface/parent/container/refcount 和失败输出/参数边界收紧；
- 64 位 process session namespace、跨 EXE 相同 handle 隔离、旧 session 延迟 teardown 保护和多 session save/load；
- Stage 5 的 stateblock、reset、surface/swapchain、资源压力 XP 回归构建脚本。

v1.7（阶段 6）新增：

- D3D8 shader model 1.x 完整前端（创建/绑定/删除/常量/反查），caps 如实宣告 `vs_1_1` 与 `ps_1_4`；
- VS 1.1 与 PS 1.1–1.4 的 bytecode → WGSL 翻译器，guest/host 双端按同一支持指令表校验，不支持的合法 opcode 明确拒绝；
- shader 专用 pipeline/bind-group/常量 uniform 路径，与固定管线共用缓存但命名空间隔离；
- shader 随 `Reset` 在新 device epoch 下重建，常量 bank 一并重发；
- Stage 6 的 XP 回归测试程序（由 `glbridge/d3d8proxy/build.sh --tests` 构建）。

本地自动验证已覆盖：guest DLL 的 XP 5.1/无 CRT 编译与 import 审计、Stage 3/4/5/6 测试程序交叉编译、协议 C/JS 一致性、device-lost 恢复、save/load checkpoint、192 次资源压力、24 次 host Reset、stale epoch 拒绝，以及 SM1.x 的逐指令数值翻译、非法 bytecode 拒绝和 shader pipeline 缓存复用。真实 GPU 页面仍位于 `glbridge/tests/d3d8_webgpu_browser_test.html`。

下一步按以下顺序关闭阶段 5/6 的真实运行门槛：

1. 同步部署 v1.7 `d3d8.dll`、`d3d8_executor.js` 和 bridge，硬刷新并确认版本串一致。
2. 运行 `build.sh --tests` 生成的 stage 5 的四个 exe（`stateblock`、`reset_lifecycle`、`lifecycle_compat`、`caps_audit`），确认标题均为 `PASS`；特别观察第二次 Reset、additional swap-chain blocker 和 RT readback。
3. 运行 stage 6 的 `d3d8_caps_audit_test.exe`（同样由 `build.sh --tests` 生成），确认标题为 `PASS`，并在真实 WebGPU 浏览器下确认生成的 shader WGSL 没有 validation error（自动测试使用 fake device，不做 WGSL 编译）。
4. 启动 MapleStory，完成登录、角色选择、至少十次地图切换和 10–30 分钟运行，记录 `resourcesLive`、pipeline/cache 和浏览器 GPU memory 是否回到稳定平台。
5. 保存 v86 state，继续改变地图/资源后再加载，确认恢复的画面和逻辑可继续绘制，且旧时间线没有闪回或 stale-handle warning。
6. 用测试注入一次 device lost，确认 replacement device 恢复后仍可 Present；任何恢复失败都必须作为明确错误上报，不能静默黑屏。
7. 若目标游戏真的调用 draw-generated GPU readback，再设计可暂停 v86 guest 的异步 readback 握手；不要在主线程用忙等或每帧 readback 破坏性能。

因此当前准确状态是：“阶段 4、5、6 已由真实 XP/浏览器验收（MapleStory v83 与 `d3d8_caps_audit_test.exe` 均通过）；阶段 7 的稳态预算优化与自动回归已落地，等待 Maple 实机 A/B 性能数据与 10–30 分钟长跑曲线后关闭阶段 7”。

## 21. 参考资料与许可边界

- [BottleShip repository](https://github.com/jenissimo/bottleship)
- [WebGPU specification](https://www.w3.org/TR/webgpu/)
- [WGSL specification](https://www.w3.org/TR/WGSL/)
- [Web Audio API](https://www.w3.org/TR/webaudio-1.0/)

BottleShip 使用 Apache-2.0。只参考架构思想时，应保持本项目实现独立；若后续直接移植源代码、WGSL 或数据表，必须记录来源、对应 commit、修改内容，并保留 Apache-2.0 所要求的版权和许可证声明。
