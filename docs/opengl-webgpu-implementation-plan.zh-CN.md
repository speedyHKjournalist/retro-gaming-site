# OpenGL 1.x / 2.x → WebGPU 完整实施方案

> **实施状态（2026-08-24）**：主机运行时已经切换为唯一的
> OpenGL → WebGPU 路径；`libglwasm/`、`Gl4esRenderer`、WebGL canvas 与后端
> 选择开关均已删除。217/217 个 guest 操作码都有 host handler，覆盖表没有 A 类
> 空缺。本文第 1 章保留的是迁移前背景，第 15 章保留原里程碑设计；当前能力与
> 可见偏差以 `glbridge/gl-webgpu/README.md` 和 `COVERAGE.md` 为准。

> 前置阅读：`d3d9-webgpu-architecture.md` 与
> `d3d9-webgpu-implementation-plan.zh-CN.md`（D9WG 协议、host executor、
> WGSL 生成与 pipeline 缓存的既有设计）、`ddraw-d3d7-webgpu-implementation-plan.zh-CN.md`
> （第三个 guest 前端复用同一后端的先例）、`glbridge/openglproxy/README.md`
> （现有 opengl32 代理已经覆盖的 API 面与它今天的限制）。
> 本文不重复其中仍然成立的结论，只写 OpenGL 特有的差异。
>
> 参考实现：ANGLE 的 WebGPU 后端（GLES2/3 → WGSL 的翻译策略、Y 翻转与深度
> 范围的处理）、Mesa `mesa/main` 的固定管线状态机（`gl_context` 的状态划分）、
> gl4es（本项目现在正在用的 GL→GLES2 转换器，它的固定管线着色器生成器是这条
> 路上唯一被真实游戏验证过的先例）。本方案与它们的关键区别：我们的 GL 客户端
> 在 v86 的 guest 里，每一次 `glGetError()` 都是一次跨虚拟机往返，所以"哪些
> 查询必须同步、能否在 host 侧本地回答"是本方案里和着色器编译同等重要的一条
> 主线。

## 1. 背景与问题定义

### 1.1 迁移前管线（历史）

```text
game.exe
  -> app-local opengl32.dll        (openglproxy/opengl32_proxy.c, 18382 行, 217 个 GLFN 操作码)
  -> v86gl.sys / PCI DMA (VGL2)    (16 MiB 连续 guest RAM + BAR doorbell)
  -> v86_network_bridge.js         (Gl4esRenderer, 约 3100 行 + 220 路 callRenderer 分发)
  -> gl4es.wasm                    (libglwasm/gl4es_bridge.c 6801 行 + 上游 gl4es ~60k 行 C)
  -> WebGL2                        (#v86gl_canvas)
```

D3D8/D3D9/DirectDraw 三条路径当时已经落到 WebGPU，而 OpenGL 仍挂在 WebGL2
上并依赖外部 C 项目 gl4es。该状态现已结束。

### 1.2 为什么要迁

1. **两套 GPU 栈并存。** 页面同时持有一个 WebGL2 上下文和一个（或两个）
   `GPUDevice`，两块 canvas 叠在 v86 屏幕上。资源、格式表、present 时机、
   save/restore 全部各写一遍，`v86_network_bridge.js` 已经长到 5997 行，其中
   大半是 GL 专用的。
2. **gl4es 的语义天花板是 GLES2。** 上游 gl4es 的设计目标是"把桌面 GL 跑在
   GLES2 硬件上"，凡是 GLES2 没有的东西它要么丢弃要么近似：累积缓冲、
   `glPolygonMode(GL_LINE)`、多边形/线型点画、`glLogicOp`、
   `GL_SELECT`/`GL_FEEDBACK`、1D 纹理、纹理边框、双面模板掩码分离。我们已经
   在 `gl4es_bridge.c` 里打了 6801 行补丁去绕开它的 3D 纹理路径、mipmap 策略
   和 sampler 命名，这条路继续走下去是在替上游维护一个分支。
3. **GLSL 只是"转发"，不是"实现"。** 现在 GLSL 源码原样交给 gl4es 的
   shaderconv，再交给浏览器的 GLSL ES 编译器。GLSL 1.20 的兼容性内建
   （`gl_ModelViewProjectionMatrix`、`gl_TexCoord[]`、`ftransform()`）能不能编
   过，取决于 gl4es 的字符串重写规则命中没有。`tests/cube2_glsl_coverage.md`
   记录的就是这件事：25/25 直接对里能过，生成器展开出来的几十个变体没有静态
   覆盖。这是"能跑几个 shader"，不是"实现了 GLSL 1.20"。
4. **WebGL2 的 draw call 开销。** 立即模式的每个顶点是一条 PCI 记录，进
   gl4es 后重新组装，再逐次 `gl.drawArrays`。半条命一帧两万个顶点时，瓶颈在
   JS 侧的记录解析和 WebGL 调用次数上，而不在 GPU。WebGPU 的 render bundle 和
   一次性 `writeBuffer` 能把这部分压掉一个数量级。
5. **架构一致性。** D3D8 是 D3D9 的翻译层，DirectDraw 是第三个前端，三者共用
   资源表、格式表、pipeline 缓存、present 路径。OpenGL 留在外面意味着每一次
   后端改进（save/restore、设备丢失恢复、shader 持久缓存、MSAA）都要写两遍。

### 1.3 目标应用

`game/` 里走 OpenGL 的镜像：

| 游戏 | 引擎 | 用到的 GL 面 |
| --- | --- | --- |
| 半条命 / 反恐精英 | GoldSrc | GL 1.1 立即模式 + `GL_ARB_multitexture` + `GL_EXT_texture_env_combine`；`glDrawPixels`/`glReadPixels` 用于截图与加载画面 |
| Cube 2: Sauerbraten | Cube 2 | GL 2.0 GLSL 1.20（几百个生成变体）、VBO、FBO/MRT、遮挡查询（2048 对象池）、ARB assembly 着色器路径、立方体贴图、深度纹理阴影 |
| 雷神之锤系 / 其它 GL 1.x demo | — | 显示列表、纹理环境、雾、点画、`GL_QUADS` |

验收顺序按 GL 面复杂度递增：`sample/gl_*_test.exe` → 半条命/CS → Cube 2。

### 1.4 一句话结论

**这次迁移的 90% 工作量在 host 侧。** guest DLL 的 18382 行、217 个操作码、
命令编码、显示列表录制、`glPushAttrib` 影子状态、`GL_SELECT`/`GL_FEEDBACK`
软件实现全部保留不动；被替换的是"操作码之后的一切"：`Gl4esRenderer`、
`gl4es_bridge.c`、gl4es 本身、WebGL2 上下文和 `#v86gl_canvas`。guest 侧唯一
必须改的是**同步查询的应答协议**（第 6 章），因为 WebGL 的 `readPixels` 是同步
的而 WebGPU 的回读不是。

## 2. 目标、非目标与成功标准

### 2.1 目标

- 新增 `glbridge/gl-webgpu/`：`gl_executor.js`（GL 状态机 + 资源表 + 固定管线
  WGSL 生成器）、`gl_shader_translator.js`（GLSL 1.20 → WGSL 真编译器）、
  `gl_arb_program.js`（ARB vertex/fragment program 汇编 → WGSL）。
- 新增 `glbridge/webgpu_host.js`：adapter/device/canvas context 的**单一所有者**，
  `d3d9_executor.js` 与 `gl_executor.js` 共用它，从而共用
  `#d3d_webgpu_canvas`。
- **完整实现 OpenGL 1.1 → 2.1 的 core profile**，加上 guest DLL 已经声明的扩展
  族。"完整"的定义见 2.3：每一个 core 入口点要么有真实实现，要么在第 16 章的偏差
  清单里有一条写明为什么不能实现、近似成了什么。
- 退役 gl4es、`libglwasm/`、`Gl4esRenderer` 和 `#v86gl_canvas`。
- 不新增第二个 `GPUDevice`，不新增第二块 canvas，不改动 VGL2 外层 ABI。

### 2.2 非目标（本方案明确不做）

- **不实现 OpenGL 3.0+**。`glGetString(GL_VERSION)` 继续报 `2.1`，GLSL 报
  `1.20`。目标游戏没有一个要求更高，而 3.x core profile 的 VAO/UBO/几何着色器
  会把 GLSL 编译器的规模再翻一倍。
- **不实现完整的 `GL_ARB_imaging`**（卷积、直方图、颜色表）。为满足 OpenGL
  1.4 能力检查，单独实现等价且范围更小的 `GL_SGI_color_matrix`：颜色矩阵栈、
  查询以及 post-color-matrix scale/bias 都会作用于像素矩形。
- **不实现颜色索引（color index）渲染模式**。`glIndex*`、
  `GL_COLOR_INDEX` 帧缓冲、`glPixelTransfer` 的索引映射。1996 年的路径，
  WGL 的 pixel format 里我们从不枚举它。注意：这与 **纹理** 的
  `GL_COLOR_INDEX8_EXT`（调色板纹理）无关，后者要做（第 11 章）。
- **不实现立体（quad-buffered stereo）与多重采样以外的辅助缓冲**
  （`GL_AUX*`）。WGL pixel format 不枚举。
- **不实现 pbuffer（`WGL_ARB_pbuffer`）**。FBO 已经在 M3 里覆盖了它的全部用途，
  目标游戏没有一个在 FBO 可用时还走 pbuffer。若 M0 的 API 追踪发现有，再单独
  立项。
- **不支持 Windows 98/ME guest**。`v86gl.sys` 是 WDM 驱动。
- **不做 WebGL2 回退**。浏览器没有 WebGPU 时页面不启用这条路径，直接报错，而不
  是悄悄退回一个语义不同的后端——这正是现在两套栈并存带来的调试灾难。

### 2.3 "完整实现"的可验收定义

对 OpenGL 1.1/1.2/1.3/1.4/1.5/2.0/2.1 core 的每一个入口点，M0 建立一张表
（`glbridge/gl-webgpu/COVERAGE.md`），每行四列：入口点、guest 是否导出、host
是否有真实实现、偏差编号。收敛条件：

- **A 类（必须真实实现）**：所有影响渲染结果的入口点。表里不允许出现"未实现"。
- **B 类（可以近似，必须登记）**：WebGPU 无法精确表达的（`glLogicOp`、
  线宽 > 1 的端点规则、`glPolygonMode` 的多边形边规则、点画的亚像素相位、
  分离的双面模板掩码、累积缓冲的位深）。每一条在第 16 章有编号、有近似方案、
  有"什么情况下会看出差别"。
- **C 类（可以是空实现，必须登记）**：纯提示类（`glHint`）、
  `glFlush` 之外的性能提示（`GL_EXT_compiled_vertex_array`）。

M7 的退出条件是这张表没有 A 类空缺，且 B/C 类每条都能在偏差清单里查到。

### 2.4 分里程碑成功标准

| 里程碑 | 成功标准 |
| --- | --- |
| M1 | `gl_triangle_test.exe`、`gl_test_depth_clear_poison.exe` 在 XP guest 里出正确画面；`#v86gl_canvas` 从页面移除 |
| M2 | `gl_rotate_cube_test.exe`、`gl_client_arrays_test.exe`、`gl_fog_material_test.exe`、`gl_blend_ui_test.exe` 全绿；半条命/CS 主菜单与一张地图能玩 |
| M3 | `gl_query_multitexture_test.exe`、`copy_tex_sub_image_3d`、`fbo_blit` 浏览器测试全绿；半条命的水面/多重纹理正确 |
| M4 | `cube2_glsl_direct_corpus_r6889.js` 的 38 对着色器全部翻译成 naga 能接受的 WGSL；Cube 2 能进地图并正确渲染世界 |
| M5 | Cube 2 的遮挡查询、ARB assembly 路径、`glReadPixels` 截图正确 |
| M6 | 累积缓冲、`glDrawPixels`/`glBitmap`、点画、`glPolygonMode`、宽线的专项测试全绿 |
| M7 | COVERAGE.md 无 A 类空缺；v86 save/load 在 GL 会话中能恢复；性能不低于现管线 |

## 3. 总体架构

### 3.1 目标管线

```text
game.exe
  -> app-local opengl32.dll        (不变：GLFN 编码、显示列表、PushAttrib、SELECT/FEEDBACK)
  -> v86gl.sys / PCI DMA (VGL2)    (不变)
  -> v86_network_bridge.js         (只剩路由：GL 记录流 -> gl_executor.submit())
  -> gl_executor.js                (GL 状态机 + 资源表 + 固定管线 WGSL + pipeline 缓存)
       + gl_shader_translator.js   (GLSL 1.20 -> WGSL)
       + gl_arb_program.js         (ARB assembly -> WGSL)
       + webgpu_host.js            (adapter/device/context，与 d3d9_executor 共用)
  -> WebGPU / WGSL                 (#d3d_webgpu_canvas)
```

### 3.2 与 D3D9 路径的复用边界

**共用（提取到 `webgpu_host.js` 或 `gpu_common.js`）：**

| 机制 | 说明 |
| --- | --- |
| adapter/device 获取与可选 feature 协商 | `texture-compression-bc`、`float32-filterable`、`float32-blendable`、`timestamp-query`，取两条路径需求的并集 |
| canvas context 的 `configure()` 与 present | 单一所有者，见 4.2 |
| 设备丢失监听与恢复 | `d3d9_executor.watchForDeviceLoss()` / `recoverDevice()` 的逻辑与 API 无关 |
| uniform ring（分块 + CPU 镜像 + 每次 submit 一发 `writeBuffer`） | `d3d9_executor` 已经为 3DMark06 的 batch-size 测试调过，GL 的立即模式压力形态相同 |
| bind group / sampler LRU 缓存 | 键不同，淘汰策略与统计相同 |
| 着色器持久缓存（IndexedDB） | 键前缀不同（`glwg.shader-cache.*`），存取代码相同 |
| 压缩纹理块尺寸/对齐、`bytesPerRow` 256 对齐的拷贝分块 | 逐字复用 |
| 半精度浮点打包、回读行打包 | 逐字复用 |
| 退役 GPU 对象的延迟销毁 | 逐字复用 |

**不共用（GL 必须自己写）：**

- GL 状态机本身。D3D9 的状态是 `SetRenderState(枚举, DWORD)`，GL 是几百个
  独立入口点加上矩阵栈、`glPushAttrib` 语义、客户端状态与服务端状态的分离。
- 固定管线 WGSL 生成器。D3D9 的纹理阶段（`D3DTOP_*`）与 GL 的
  `GL_TEXTURE_ENV`（`GL_MODULATE`/`GL_COMBINE`/`GL_DOT3_RGB`/crossbar）**看起来
  像但语义不同**：GL 的 combine 有独立的 RGB/Alpha 函数、per-arg 的 operand
  （`SRC_COLOR`/`ONE_MINUS_SRC_COLOR`/`SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`）、
  `RGB_SCALE`/`ALPHA_SCALE`，以及 `GL_PREVIOUS`/`GL_CONSTANT`/`GL_TEXTURE<n>`
  的源选择。照抄 D3D9 的表会在半条命的 detail texture 上出错。
- 光照。GL 的光照模型（8 光源、`GL_LIGHT_MODEL_TWO_SIDE`、
  `GL_LIGHT_MODEL_LOCAL_VIEWER`、`GL_COLOR_MATERIAL` 的跟踪、
  `GL_SEPARATE_SPECULAR_COLOR`、`GL_RESCALE_NORMAL`/`GL_NORMALIZE`）与 D3D9 的
  接近但不相同，尤其是二次衰减项与聚光灯指数。
- 着色器编译器。D3D9 编译的是字节码，GL 编译的是文本。没有任何一行可复用。

### 3.3 新增与退役文件

**新增：**

```text
glbridge/webgpu_host.js                 GPU 设备/canvas 单一所有者（两条路径共用）
glbridge/gl-webgpu/gl_executor.js       GL 状态机、资源表、固定管线、pipeline 缓存
glbridge/gl-webgpu/gl_shader_translator.js  GLSL 1.20 -> WGSL
glbridge/gl-webgpu/gl_arb_program.js    ARB vertex/fragment program -> WGSL
glbridge/gl-webgpu/gl_shader_worker.js  翻译放到 worker（与 d3d9 同构）
glbridge/gl-webgpu/COVERAGE.md          入口点覆盖表（2.3）
glbridge/gl-webgpu/README.md            偏差清单的权威记录
```

**修改：**

```text
glbridge/openglproxy/opengl32_proxy.c      仅：响应区协议（第 6 章）、能力查询改由 WebGPU 决定、
                                        可选的立即模式批量记录
glbridge/openglproxy/v86gl_ioctl.h         GLWG 响应区常量
glbridge/v86_network_bridge.js          删除 Gl4esRenderer 与 callRenderer 分发，改为路由
retro-gaming-site/game.html             移除 gl4es.js / gl4es_loader.js / #v86gl_canvas
retro-gaming-site/app.js                bridge 安装参数
glbridge/d3d9-webgpu/d3d9_executor.js   改为从 webgpu_host 取 device/context，不再自己 configure
```

**退役：**

```text
glbridge/libglwasm/                     整个目录（gl4es_bridge.c 6801 行 + 构建脚本 + 产物）
Code/gl4es/                             外部依赖，从构建链路移除
```

## 4. 核心架构决策

### 4.1 guest DLL 不重写；这次迁移是"换后端"，不是"换前端"

D3D8 迁到 D9WG 时换掉的是 guest 侧的命令发射器；这次相反。原因很直接：
`opengl32_proxy.c` 的 217 个操作码就是 OpenGL 的 API 面本身，它与后端无关。
它已经做对的、且必须保留的东西：

- **显示列表**在 guest 侧录制成记录流（`emit_display_list_stream`），
  `glCallList` 重放。这个位置是对的：列表内容不含指针，重放不需要 host 参与。
- **`glPushAttrib`/`glPopAttrib`/`glPushClientAttrib`** 在 guest 侧的影子状态上
  完成，只把差异发出去。这避免了 host 维护一份属性栈。
- **`GL_SELECT`/`GL_FEEDBACK`** 是 guest 侧的软件变换。它们要求 CPU 侧知道
  完整的模型视图/投影矩阵和视口，本来就不该上 GPU。
- **PBO 的 pack/unpack 偏移解析**在 guest 侧完成，传输的是最终字节。
- **VBO 内容留在 guest 内存**，画的时候打包进客户端数组记录。

**这一条有一个例外要在 M2 里改**：VBO 目前每次 draw 都重新打包上传。WebGPU 有
真正的 `GPUBuffer`，把 VBO 提升成 host 资源（`glBufferData` 直接
`writeBuffer`）是 M2 的性能项，见 13.2。

### 4.2 一个 `GPUDevice`、一块 canvas，所有者是 `webgpu_host.js`

现状里 `d3d8_executor` 和 `d3d9_executor` 各自 `requestDevice()` 并各自
`context.configure()`，`v86_network_bridge.js` 有一大段注释解释"两者共用一块
canvas 时后 configure 的赢，另一个的帧静默消失"。再加一条 GL 路径会把这个
隐患变成三方。

`webgpu_host.js` 提供：

```js
const host = await V86GPUHost.acquire(canvas, {
    requiredFeatures: ["texture-compression-bc", "float32-filterable", ...],
});
// host.adapter / host.device / host.format / host.context
// host.claimPresenter(name) -> 返回一个 token；present 时校验
// host.releasePresenter(token)
```

规则：

1. `acquire()` 幂等。第二个调用者拿到同一个 device 与同一个已 configure 的
   context，feature 集合取并集（首次创建时就按并集申请，避免 device 重建）。
2. **谁在 present 谁是 presenter。** 一个 guest 进程只会加载
   `opengl32.dll`/`d3d8.dll`/`d3d9.dll`/`ddraw.dll` 中的一个（部署互斥规则不
   变），所以正常情况下只有一个 presenter。第二个 executor 第一次 present 时
   `claimPresenter` 失败，打一条明确的 error 并**继续渲染到自己的离屏后备缓冲**
   ——这样至少 `getStats()`/`dumpSmallTextures()` 还能诊断，而不是黑屏加沉默。
3. 后备缓冲是 executor 自己的纹理，不是 `context.getCurrentTexture()`
   （`d3d9_executor.ensureBackBufferTexture()` 已经是这个设计）。GL 也这么做，
   因为 GL 允许在 present 之间反复读回后备缓冲（`glReadPixels`、
   `glCopyTexImage2D`），而 swapchain 纹理在 present 后就失效了。
4. `d3d9_executor` 的改动只有两处：构造时接受 `host`，`initialize()` 里跳过
   `requestAdapter`/`configure`。它已经支持注入 `device`/`context`/`format`
   选项，所以是加一层薄封装，不是重构。

### 4.3 坐标系：在顶点着色器里翻一次 Y，其余全部按 GL 语义写

这是整条路径上最容易埋下系统性 bug 的地方，必须一次定死。

| | OpenGL | WebGPU |
| --- | --- | --- |
| NDC | y 向上，z ∈ [-1, 1] | y 向上，z ∈ [0, 1] |
| 帧缓冲行 0 | 底 | 顶 |
| `gl_FragCoord.y` / `@builtin(position).y` | 从底算 | 从顶算 |
| 纹理 (0,0) / v=0 | 底（`glTexImage2D` 的第一行是底行） | 顶（第一行是 v=0） |
| 视口/裁剪矩形原点 | 左下 | 左上 |

**决策：在 clip space 取反 y（`pos.y = -pos.y`），并把正面绕向反过来。**

推论（全部是"什么都不用改"，这正是选它的理由）：

- 帧缓冲物理行 0 = GL 的底行。于是 `glReadPixels(x, y, ...)` 的 y（从底算）直接
  就是 WebGPU 纹理的行号，回读不需要翻转。
- `glViewport`/`glScissor` 的左下原点直接就是 WebGPU 的左上原点值，不需要
  `height - y - h` 换算。
- `@builtin(position).y` 从物理顶行算 = 从 GL 底行算 = `gl_FragCoord.y`，
  GLSL 里对 `gl_FragCoord.y` 的使用（Cube 2 的后处理、软粒子）不需要改写。
- 渲染到纹理后再采样：写进去的第一行是 GL 的底行，采样时 v=0 取到的也是那一
  行，而 GL 的 t=0 就是底行。**一致。**
- `glCopyTexSubImage2D` 从帧缓冲拷到纹理是行对行的直接拷贝。

代价两条，都是局部的：

- **绕向**：`glFrontFace(GL_CCW)` 在 WebGPU 上要写成 `frontFace: "cw"`，反之
  亦然。集中在一个函数里（`gpuFrontFace(state)`），并且这是唯一一处允许出现
  绕向翻转的地方。
- **present 要翻**：把后备缓冲搬到 swapchain 纹理时做一次上下翻转的全屏 blit。
  present 本来就是一次 blit（要做 MSAA resolve、sRGB 转换、光标合成），翻转是
  免费搭车。

**深度**：GL 的 clip-space z ∈ [-w, w]，WebGPU 是 [0, w]。统一在
`gl_Position` 产出之后立刻做 `pos.z = (pos.z + pos.w) * 0.5`。固定管线里这一步
可以烘进投影矩阵，但**不要**这么做——用户程序会调 `glGetFloatv(GL_PROJECTION_MATRIX)`
把矩阵读回去，读回来的必须是他给的那个。所以统一在着色器尾部做，固定管线与
GLSL 路径共用同一段代码，第 8.7 节。`glDepthRange(n, f)` 映射到
`setViewport(..., minDepth=n, maxDepth=f)`，两边都是 [0,1]，直通。

### 4.4 GL 状态机在 host，pipeline 由状态签名派生

WebGPU 的管线是不可变对象，GL 是一台大状态机。桥接方式与 `d3d9_executor`
相同：命令只更新状态并置脏位，真正的 `GPURenderPipeline` 在 draw 时按签名
查缓存。

签名字段（缺一个就会出现"状态改了画面没变"的幽灵 bug，逐条列出以便审查）：

```text
program            用户 GLSL 程序 handle / ARB 程序对 / 固定管线签名（见下）
vertexLayout       每个启用的属性：location, format, offset, stride, stepMode
topology           展开后的图元（见 4.6）
frontFace/cullMode glFrontFace + glCullFace + GL_CULL_FACE
depth              GL_DEPTH_TEST, glDepthFunc, glDepthMask
stencil            GL_STENCIL_TEST, 前/后 func+ref 掩码、op 三元组、写掩码
blend              每个 draw buffer：GL_BLEND, 分离 func、分离 equation
colorWrite         每个 draw buffer 的 glColorMask
polygonOffset      GL_POLYGON_OFFSET_FILL/LINE/POINT + factor/units（WebGPU 的
                   depthBias/depthBiasSlopeScale 是 pipeline 常量，必须进签名）
multisample        sampleCount, alphaToCoverage, sampleMask
targets            每个颜色附件的 GPUTextureFormat + 深度模板格式
unclippedDepth     GL_DEPTH_CLAMP（需要 depth-clip-control feature）
```

固定管线签名（决定生成哪段 WGSL）另外还要包含：光照开关与每个光源的类型
（方向/点/聚光）、`GL_COLOR_MATERIAL` 的跟踪面与模式、双面光照、局部视点、
分离高光、法线归一化模式、雾模式与坐标源、每个启用纹理单元的目标类型与
`GL_TEXTURE_ENV` 完整状态、`glTexGen` 的四个坐标各自的模式、纹理矩阵是否为
单位阵、alpha test 的函数、启用的裁剪平面位掩码、点大小衰减。

`gl_executor` 里这两张签名各写一个 `signatureOf()` 函数并附一句注释说明"改了
状态却没进签名"是什么症状——这是 D3D9 路径上花过时间的地方。

### 4.5 固定管线是"生成 WGSL"，不是"模拟"

固定管线不做解释器，按签名生成一段 WGSL 并缓存。生成器分两半：

- **顶点**：模型视图/投影变换、法线变换（`inverse-transpose` 上传，不在着色器
  里求逆）、8 光源的完整 GL 光照方程、颜色材质跟踪、纹理坐标生成与纹理矩阵、
  雾坐标、点大小衰减、裁剪平面距离。
- **片段**：按启用的纹理单元顺序求值 `GL_TEXTURE_ENV`（含 `GL_COMBINE` 的
  RGB/Alpha 双通道、operand、scale、crossbar 源），然后是分离高光加法、雾混合、
  alpha test 的 `discard`、裁剪平面的 `discard`。

参照对象是 `d3d9_executor.js` 的 `buildFixedFunctionVertexShader` /
`buildFixedFunctionPixelShader` 的**结构**（uniform 块布局、varying 分配、
条件拼接的写法），而不是它的语义表。

### 4.6 WebGPU 没有的图元一律在索引层展开

WebGPU 只有 point-list / line-list / line-strip / triangle-list /
triangle-strip。GL 多出来的：

| GL 图元 | 展开 |
| --- | --- |
| `GL_QUADS` | 每 4 顶点 → 2 三角形（0,1,2 / 0,2,3），保持 provoking vertex 见下 |
| `GL_QUAD_STRIP` | 每 2 顶点对 → 2 三角形 |
| `GL_POLYGON` | 扇形三角化（0,i,i+1），GL 只保证凸多边形 |
| `GL_TRIANGLE_FAN` | 展开成 triangle-list（WebGPU 无 fan） |
| `GL_LINE_LOOP` | line-strip + 回到首点的一条边 |

展开在 host 侧生成索引缓冲，缓存在按 (图元, 顶点数) 键的池里——`GL_QUADS`
的索引只与顶点数有关，半条命一帧里成千上万次 `glBegin(GL_QUADS)` 共用同一份。

**平面着色（`glShadeModel(GL_FLAT)`）的 provoking vertex**：WGSL 的
`@interpolate(flat)` 默认取三角形的**第一个**顶点（`flat, first`），所以三角化
时索引顺序必须让 GL 规定的那个顶点排在首位。GL 2.1 规范的平面着色表逐图元给出
取哪个顶点，不是一句"取第一个"能概括的，实现时必须照表来：

| GL 图元（顶点 1 起编号） | 第 i 个图元取的顶点 |
| --- | --- |
| `GL_TRIANGLES` | 3i − 2（三角形的第一个） |
| `GL_TRIANGLE_STRIP` | i（三角形的第一个） |
| `GL_TRIANGLE_FAN` | i + 1（三角形的**第二个**，即非扇心的那一端） |
| `GL_QUADS` | 4i（四边形的**最后一个**） |
| `GL_QUAD_STRIP` | 2i + 2 |
| `GL_POLYGON` | 1（整个多边形共用第一个顶点） |
| `GL_LINES` | 2i − 1 |
| `GL_LINE_STRIP` / `GL_LINE_LOOP` | i（闭合段取第 n 个） |

`GL_TRIANGLE_FAN` 与 `GL_QUADS` 这两行是最容易写错的：前者不是扇心，后者不是
第一个。做错的症状只有开了平面着色的模型才看得见——颜色整体错位一个顶点，很难
定位。M2 的 `gl_flat_shading_test.exe` 逐图元测这张表。

### 4.7 立即模式与客户端数组：host 侧组装，一次上传

`glBegin`/`glVertex*` 的每个顶点今天是一条 PCI 记录。host 侧的处理：

1. `glBegin(mode)` 开一个当前顶点累加器。当前颜色/法线/纹理坐标/雾坐标/次颜色
   是 GL 的"当前值"状态，每次 `glVertex*` 快照一份写进一个交错的 CPU 数组。
2. `glEnd` 时决定顶点格式（哪些属性在这一批里被写过 → 决定 vertexLayout 签名），
   把 CPU 数组写进当帧的 vertex ring，按 4.6 拿索引，发一次 draw。
3. **连续的兼容批次合并**：相邻的 `glBegin(GL_TRIANGLES)`…`glEnd` 之间如果没有
   任何改变 pipeline 签名或 bind group 的命令，就继续往同一个顶点缓冲追加，
   `glEnd` 不立即发 draw，等到状态真的变了或者帧结束才发。GoldSrc 的世界渲染
   是成百上千个连续的 `GL_TRIANGLE_FAN`/`GL_POLYGON`，这一条能把 draw call 从
   千级压到十级。

客户端数组（`glDrawArrays`/`glDrawElements` + `gl*Pointer`）guest 侧已经把数据
打包进记录，host 侧直接写进同一个 ring。**M2 的性能项**：`glBufferData` 的 VBO
提升为常驻 `GPUBuffer`，`glDrawElements` 直接引用，不再每帧重传（13.2）。

顶点属性槽位固定分配，写死在一张表里（固定管线与 GLSL 兼容内建共用）：

```text
0  gl_Vertex          8..15  gl_MultiTexCoord0..7
2  gl_Normal          （generic attrib N 若与上面冲突，见 4.12 的冲突规则）
3  gl_Color
4  gl_SecondaryColor
5  gl_FogCoord
```

这是 NVIDIA 的经典别名布局，也是绝大多数 GL 2.0 程序假定的那个。
`glBindAttribLocation` 显式指定时以用户的为准；用户未指定且 GLSL 里同时用了
`gl_Vertex` 与 generic attribute 时，generic 从 16 往下倒序分配以避开别名冲突，
超出 `maxVertexAttributes`（16）时链接失败并报明确错误。

### 4.8 同步查询：能在 host 本地回答的绝不上 GPU

guest 的每一次 `glGetError()`/`glGetIntegerv()` 都是一次 VM 往返，而 WebGPU
的任何 GPU→CPU 读取都是异步的。分三类：

**第一类：host 侧 JS 状态即可回答，同步返回（占 95%）。**
`glGetError`、`glGetIntegerv`/`glGetFloatv`/`glGetBooleanv` 的全部状态查询、
`glGetString`、`glIsEnabled`、`glGetTexParameter*`、`glGetTexLevelParameter*`、
`glGetMaterial*`/`glGetLight*`/`glGetTexEnv*`/`glGetTexGen*`、`glGetClipPlane`、
矩阵查询。这些今天由 guest 的影子状态或 gl4es 回答；迁移后**权威副本在 host 的
GL 状态机里**，guest 影子只作为发送前的冗余消除。

**第二类：需要 host 侧的编译/链接结果，仍可同步回答。**
`glGetShaderiv(GL_COMPILE_STATUS)`、`glGetProgramiv(GL_LINK_STATUS)`、
`glGetShaderInfoLog`、`glGetUniformLocation`、`glGetAttribLocation`、
`glGetActiveUniform`/`glGetActiveAttrib`、`glCheckFramebufferStatus`。
关键判断：**编译/链接的成败由我们自己的 GLSL 编译器决定，不等 WebGPU。**
`createShaderModule()` 是同步的，`getCompilationInfo()` 是异步的；我们不等它，
因为我们的翻译器如果产出了 WGSL，那就是"链接成功"。WGSL 层面的错误是**我们的
bug**，不是用户程序的错误，应该以 console.error + 第 16 章的偏差登记出现，
而不是变成一个 guest 看得见的 `GL_INVALID_OPERATION`。这也是为什么
`gl_shader_wgsl_validation_test.js`（naga）在 CI 里是硬性门槛。

**第三类：真的需要 GPU 往返，走响应区 + guest 自旋（第 6 章）。**
只有三个：`glReadPixels`、遮挡查询结果（`glGetQueryObjectuiv`）、
`glFinish`。协议逐字沿用 D9WG 的设计（`status` 字段写在最后、host 每批次递增
心跳计数、超时看的是"host 是否还在推进"而不是墙钟）。

### 4.9 GLSL 1.20 必须写真编译器

不做正则替换、不做逐行改写。理由：Cube 2 的着色器是 CubeScript 在运行时拼出来
的，`tests/cube2_glsl_coverage.md` 明确记录了"生成器展开的几十个变体没有静态
覆盖"；任何基于模式匹配的方案在这些变体上都会以不可预测的方式失败。

编译器是一条完整的前端：词法 → 预处理器（`#define`/`#if`/`#ifdef`/`#elif`/
`#else`/`#endif`/`#extension`/`#version`/`#line`，宏含参数与展开）→ 语法分析 →
类型检查（含 GLSL 的隐式转换规则与函数重载决议）→ AST → WGSL 代码生成。
细节见第 8 章。规模预期 4000–5500 行 JS，是本方案单体最大的一块。

### 4.10 非一致控制流里的纹理采样

GLSL 1.20 允许在 `if`/循环里调 `texture2D()`（隐式 LOD）。WGSL 要求
`textureSample` 只出现在**一致控制流**（uniform control flow）中。这不是理论问
题：Cube 2 的 bump/water/glare 着色器大量使用条件采样。

处理策略，按优先级：

1. **提升（hoist）**：采样的坐标与采样器在分支外就能求值时，把
   `textureSample` 提到分支外，分支里只用结果。这覆盖绝大多数情况。
2. **显式导数**：提不出来时改用 `textureSampleGrad(t, s, uv, dpdx(uv), dpdy(uv))`，
   其中 `dpdx/dpdy` 在分支外算。语义与 GL 一致（GL 在非一致控制流下的隐式导数
   本来就是未定义的，取分支外的导数是最接近硬件实际行为的选择）。
3. **循环内**：改 `textureSampleLevel(..., 0.0)` 并在偏差清单登记——只有当着色器
 在循环里对带 mipmap 的纹理做隐式 LOD 采样时才有可见差别。

翻译器必须显式跟踪"当前是否在一致控制流里"，并在第 3 种情况发生时打点计数
（`getStats().nonUniformSamples`），否则这类近似会静默积累。

### 4.11 varying 的打包与 16 槽上限

WebGPU 的 `maxInterStageShaderVariables` 是 16（vec4 计）。GLSL 1.20 的兼容
varying 有 `gl_TexCoord[0..7]`（8）+ `gl_FrontColor`/`gl_BackColor`/
`gl_FrontSecondaryColor`/`gl_BackSecondaryColor`（4）+ `gl_FogFragCoord`（1），
已经 13 个，再加用户 varying 就会超。

规则：

1. **只为被两端实际使用的 varying 分配槽位。** 编译器在 AST 上做活跃性分析，
   顶点端写了但片段端没读的直接丢弃。这一条就把 `gl_TexCoord[]` 从 8 降到实际
   用到的 2–3 个。
2. **标量与小向量打包**。`float`/`vec2`/`vec3` 的用户 varying 按贪心装箱塞进
   vec4 槽。打包表进入程序的反射结构，两端必须由同一次链接产出（GL 的链接语义
   本来就要求 VS/FS 一起链接，这里正好利用）。
3. **背面颜色**（`gl_BackColor`）只在 `GL_LIGHT_MODEL_TWO_SIDE` 且片段端读
   `gl_Color` 时才占槽，用 `@builtin(front_facing)` 在片段端二选一。
4. 超出 16 时链接失败，`glGetProgramInfoLog` 返回明确文本。这是 B 类偏差
   （真实 GL 2.0 硬件的下限是 `GL_MAX_VARYING_FLOATS` = 32 floats = 8 vec4，
   我们的 16 反而更宽松），但要在偏差清单里写清楚。

### 4.12 绑定组布局固定为四组

`maxBindGroups` 是 4。固定分配，永不动态改：

```text
group(0)  帧/视图级：视口参数、present 相关常量、深度范围        —— 极少变
group(1)  程序 uniform：固定管线的状态块，或 GLSL 程序的默认 uniform 块
group(2)  纹理 + 采样器：每个纹理单元一对 binding（texture_2d / sampler），
          外加深度比较采样器的独立 binding
group(3)  预留（M5 的 ARB program env/local 参数、M6 的点画/DrawPixels 辅助资源）
```

`maxSampledTexturesPerShaderStage` 与 `maxSamplersPerShaderStage` 通常是 16，
GL 2.0 只要求 `GL_MAX_TEXTURE_IMAGE_UNITS` ≥ 2、`GL_MAX_COMBINED_*` ≥ 2；我们
声明 8 个固定管线单元 + 16 个片段采样器，与 guest 现在声明的 8 单元一致。

固定管线的 uniform 块用一个大结构体（矩阵 + 光源数组 + 材质 + 雾 + texenv 常量
+ 裁剪平面 + alpha 参考值），走 uniform ring 的一个 slot。GLSL 程序的
`glUniform*` 写进程序自己的影子数组，draw 时整块进 ring——GL 的 uniform 是程序
对象状态，不是"缓冲"，所以脏跟踪按程序做，用与 `d3d9_executor.commandSerial`
相同的手法（任何非 draw 命令递增序号，draw 时比对）。

### 4.13 多上下文与 `wglShareLists`

现状（README 承认）：WGL 的 current context/DC 按线程跟踪，但命令流、影子状态、
表面选择、渲染器都是进程全局的，两个 HGLRC 交替时会互相看到对方的状态。

这次一并修掉，因为 host 侧本来就要重建全部状态：

- `gl_executor` 维护 `contexts: Map<handle, GLContextState>`，每个上下文有完整
  独立的状态机快照。
- **对象命名空间按"共享组"划分**：纹理、缓冲、显示列表、着色器/程序、渲染缓冲
  属于共享组（`wglShareLists` 把两个上下文并进同一组）；FBO、顶点数组状态、
  查询对象**不共享**（与桌面 GL 一致）。
- `wglMakeCurrent` 只切换 host 侧的当前上下文指针，不产生 GPU 工作。
- 每个上下文有自己的后备缓冲（因为每个 HDC 对应一个 guest 窗口）。present 时按
  `wglSwapBuffers(hdc)` 找到对应的后备缓冲再 blit。

WineD3D 的短命能力探测上下文（README 提到的那个）在这个模型下自然正确：它的
上下文销毁不影响另一个上下文的状态。

### 4.14 能力必须真实

`glGetString(GL_EXTENSIONS)` 与所有 `GL_MAX_*` 由 **host 的 WebGPU 实现**决定，
guest 不再猜：

- `GL_EXT_texture_compression_s3tc`：仅当 adapter 有 `texture-compression-bc`
  时声明；没有时**不声明**并保留 CPU 解码路径作为上传时的兼容（现状已有），
  但不再谎报扩展。
- `GL_ARB_texture_float` / `GL_ARB_half_float_pixel`：仅当浮点目标可渲染
  且 `float32-filterable` 可用时声明。
- `GL_EXT_texture_filter_anisotropic` 与 `GL_MAX_TEXTURE_MAX_ANISOTROPY`：
  WebGPU 的 `maxAnisotropy` 恒定支持到 16，直接声明。
- `GL_MAX_TEXTURE_SIZE` = `limits.maxTextureDimension2D`；
  `GL_MAX_3D_TEXTURE_SIZE` = `maxTextureDimension3D`；
  `GL_MAX_DRAW_BUFFERS`/`GL_MAX_COLOR_ATTACHMENTS` = `maxColorAttachments`
  （截到 8）。
- `GL_MAX_VERTEX_ATTRIBS` = 16；`GL_MAX_TEXTURE_UNITS` = 8（固定管线）；
  `GL_MAX_TEXTURE_IMAGE_UNITS` = 16。
- `GL_ARB_vertex_program`/`GL_ARB_fragment_program` 只在 M5 完成后声明。
- **`GL_ARB_occlusion_query` 报告的样本数**：WebGPU 的遮挡查询语义是
  "any samples passed"，我们像现在一样把可见结果饱和成一个大样本数
  （见 16 章 D-07）。

`V86GL_QUERY_HOST_CAPABILITIES` 的位定义改成 WebGPU 术语（BC 压缩、
float32-filterable、float32-blendable、depth-clip-control、timestamp-query），
guest 侧把结果夹逼到自己声明的上限，逻辑不变。

### 4.15 拒绝要显式、可见

任何 host 侧无法执行的命令，走统一的 `refuse(op, reason, details)`：一次
`console.error`（带足够定位的字段）、`getStats().refusals` 计数、并在
`glGetError()` 的返回里体现（若 GL 规范对该情形定义了错误码）。**绝不静默
忽略**——DDraw 与 D3D8 两次迁移里最费时的调试都是静默忽略造成的。

## 5. 阶段 0：基线与 API 面追踪

M0 不写渲染代码，只建立"我们现在到底在实现什么"的事实基础。

### 5.1 入口点覆盖表

从 `openglproxy/opengl32.def` 与 `opengl32_proxy.c` 的 `wglGetProcAddress` 表导出
全部导出名，与 GL 1.1–2.1 core 的规范列表对拍，生成 `gl-webgpu/COVERAGE.md`
的初版（2.3 的四列）。缺失的 core 入口点在这里第一次被列出来——预期会有一批
`glGet*` 的变体和 `glTexGend`/`glRasterPos` 系列的重载。

### 5.2 真实调用面采集

在现有 `Gl4esRenderer` 的 `callRenderer` 里加一个直方图（GLFN → 调用次数、
字节数），跑：

- `sample/gl_*.c` 编出的全部 10 个测试程序
- 半条命：主菜单 + 一张地图 60 秒
- 反恐精英：一局 bot 对战 60 秒
- Cube 2：启动 + 进入一张地图 + 移动 60 秒

产出 `docs/opengl-call-profile.md`：每个游戏用到的操作码集合、每帧命令数、每帧
字节数、立即模式顶点数、纹理上传字节数、同步查询次数。这张表决定 M1–M6 的实现
顺序，也给第 13 章的性能预算一个真实的分母。

**特别要采的三个数**：(a) 每帧同步查询次数（决定 4.8 的第一类优化值不值）、
(b) 立即模式顶点数 vs 客户端数组顶点数（决定 4.7 合并策略的收益）、
(c) `glBufferData`/`glBufferSubData` 的每帧字节数（决定 VBO 提升的优先级）。

### 5.3 GLSL 语料

把 Cube 2 的 `data/glsl.cfg`（已有 SHA-256 记录）与运行时实际链接的所有着色器
对全部 dump 出来（在 `GLFN_SHADER_SOURCE` 处落盘），建成
`tests/cube2_glsl_runtime_corpus.js`。现有的
`cube2_glsl_direct_corpus_r6889.js` 只有静态可展开的 38 对，运行时语料才是 M4
的真实验收集合。半条命没有 GLSL，但要采它的 `GL_TEXTURE_ENV` 组合集合。

### 5.4 A/B 开关

`installV86GLNetworkBridge` 加一个选项 `glBackend: "gl4es" | "webgpu"`
（默认先是 `gl4es`，M2 之后翻转）。两个后端接同一个记录流，随时可切回去对比
截图。这个开关在 M7 才删除。**这是本方案唯一的风险出口**：任何一个里程碑做砸
了，页面改一个字符串就能回到今天的行为。

### 阶段 0 退出条件

- `COVERAGE.md` 初版存在，A 类空缺已列出。
- `opengl-call-profile.md` 有四个应用的真实数据。
- 运行时 GLSL 语料落盘，naga 能对它们跑一遍（此时预期全部失败，作为 M4 的基线）。
- A/B 开关可用，`glBackend: "webgpu"` 能走通"收到记录 → 打印 → 什么都不画"。

## 6. GLWG：协议增量

### 6.1 不动的部分

外层 VGL2 描述符、`v86gl.sys`、PCI BAR、`v86gl-pci-frame` 事件、记录编码
（`u16 fn` + `u16 size`，`size == 0xFFFF` 时后随 `u32` 扩展长度）、217 个已有
操作码的载荷布局——**全部不变**。D3D8/D3D9/DDraw 用 `0xFFE0`/`0xFFE1` 外层
功能码把自己的批次包起来；GL 的记录流是"裸"的，这是历史上的先来后到，保持
不变即可（`pushPCIBatch` 已经是"先认信封，都不是就当 GL"）。

保持裸流有一个实际好处：`v86_network_bridge.js` 的 GL 状态日志
（`stateJournal`，v86 save/load 用它重放 GL 状态）存的就是这些记录，与后端无关，
**换后端后它逐字继续有效**。这是 D3D9 路径至今缺失的能力（见 17 章），GL 路径
不能在迁移中把它弄丢。

### 6.2 新增：响应区（唯一必须改 guest 的地方）

DMA 竞技场的尾部 4 MiB 划为响应区，布局与 D9WG 逐字对齐，只换前缀：

```c
/* openglproxy/v86gl_ioctl.h 新增 */
#define GLWG_RESPONSE_REGION_BYTES (4u * 1024u * 1024u)
#define GLWG_SLOT_BYTES            16u
#define GLWG_SLOT_COUNT            1024u
#define GLWG_QUERY_REGION_BYTES    (GLWG_SLOT_BYTES * GLWG_SLOT_COUNT)
#define GLWG_READBACK_REGION_OFFSET GLWG_QUERY_REGION_BYTES
#define GLWG_HEARTBEAT_BYTES       16u
#define GLWG_HEARTBEAT_OFFSET      (GLWG_RESPONSE_REGION_BYTES - GLWG_HEARTBEAT_BYTES)

#define GLWG_RESPONSE_PENDING 0u
#define GLWG_RESPONSE_OK      1u
#define GLWG_RESPONSE_FAILED  2u
```

`emit_pci_batch` 的 `command_capacity` 相应改为
`g_dma_capacity - sizeof(V86GLDMADesc) - GLWG_RESPONSE_REGION_BYTES`。
这使 GL 路径的竞技场布局与 D3D 路径完全一致（后者已经在用尾部 4 MiB），代价是
命令区从 ~16 MiB 降到 ~12 MiB——`opengl-call-profile.md` 会证实这远高于任何一
帧的实际用量。

响应结构与 D9WG 同形，`status` 字段**必须放在最后**：`emulator.write_memory()`
按地址递增拷贝，guest 观察到 `OK` 时数据一定已经落地。

```c
typedef struct GLWGResponse {
    uint32_t request_id;
    uint32_t byte_count;   /* 查询时复用为 value_low */
    uint32_t reserved;     /* 查询时复用为 value_high */
    volatile uint32_t status;
    /* readback：byte_count 字节数据紧随其后 */
} GLWGResponse;
```

心跳：host 每处理完一批递增 `GLWG_HEARTBEAT_OFFSET` 处的计数器；guest 的自旋
超时判据是"心跳在 N 毫秒内没有推进"，而不是墙钟——理由与 D9WG 注释里写的一样，
一个落后几千批的 host 是慢，不是坏。

### 6.3 走响应区的三条命令

| 命令 | 现状 | 迁移后 |
| --- | --- | --- |
| `GLFN_READ_PIXELS` (94) | host 同步 `gl.readPixels` 写回记录 | 载荷加 `response_offset` + `request_id`；host `copyTextureToBuffer` + `mapAsync`，完成后写响应区；guest 自旋 |
| `GLFN_QUERY_OBJECT_BATCH` (216) | 同步刷新全部待决查询 | 同上；host 用 `resolveQuerySet` + `mapAsync` 一次解决整批（Cube 2 的 2048 对象池按批解决这一点必须保留） |
| `glFinish` (`GLFN_FINISH`) | 立即返回 | 载荷加响应槽；host 在 `queue.onSubmittedWorkDone()` 后应答 |

`glFlush` 不走响应区：它只保证命令进入管线，host 侧 `queue.submit()` 后立即
返回即可。

**第 4.8 节第一/第二类查询的记录不变**：它们今天已经是"host 同步写回记录"，
而新 host 同样能同步回答（答案在 JS 状态里，不碰 GPU），所以
`GLFN_QUERY_INTEGER`/`QUERY_GL_STRING`/`QUERY_ERROR`/`QUERY_LOCATION`/
`QUERY_UNIFORM`/`QUERY_OBJECT_IV`/`QUERY_OBJECT_LOG`/`QUERY_ACTIVE`/
`CHECK_FRAMEBUFFER_STATUS`/`QUERY_PROGRAM_*_ARB` 的载荷一个字节都不用改。
这是把"权威状态放在 host"这个决定换来的最大红利。

### 6.4 新增：立即模式批量记录（性能项，M2）

`GLFN_IMMEDIATE_BATCH = 218`：一条记录携带一整个 `glBegin`…`glEnd`。

```c
typedef struct GLWGImmediateBatch {
    uint32_t mode;          /* GL_TRIANGLES / GL_QUADS / ... */
    uint32_t vertex_count;
    uint32_t attrib_mask;   /* 位 0 位置，1 法线，2 颜色，3 次颜色，4 雾坐标，
                               8..15 纹理坐标 0..7 */
    uint32_t vertex_stride; /* 交错顶点的字节步长 */
    /* vertex_count * vertex_stride 字节交错顶点数据 */
} GLWGImmediateBatch;
```

guest 侧在 `glBegin` 时开一个累加器，逐个 `glVertex*` 快照当前属性，`glEnd` 发
一条记录。收益：半条命一帧两万顶点从两万条记录（每条 4 字节头 + 12 字节载荷）
降到几百条记录，PCI 字节数降约 25%，host 侧的记录解析次数降两个数量级。

**这条是可选路径**：老的逐顶点记录必须继续被 host 接受（显示列表里已经录着它
们，`stateJournal` 里也有）。guest 通过 `V86GL_QUERY_HOST_CAPABILITIES` 得知
host 支持批量记录后才启用。

### 6.5 新增：VBO 提升（性能项，M2）

现状：`glBufferData` 的内容留在 guest，draw 时打包进客户端数组记录。
迁移后 `GLFN_BUFFER_DATA`(197)/`GLFN_BUFFER_SUB_DATA`(198) 的载荷直接带数据，
host 建立常驻 `GPUBuffer`；`GLFN_DRAW_ELEMENTS_DIRECT`(207) 等只带偏移。
载荷布局已经支持（`VBOPointerPayload` 里就是 offset），**要改的是 guest 不再在
draw 时回填数据**。guest 仍需保留一份副本以应答 `glGetBufferSubData` 与
`glMapBuffer`（GL 1.5 的映射语义在 guest 侧用影子内存 + 解映射时上传实现，
与 DDraw 的 Lock/Unlock 同构）。

### 6.6 解析器安全

host 侧解析所有记录时逐条校验载荷长度、句柄有效性、枚举值域、以及任何来自 guest
的尺寸/计数（纹理宽高、顶点数、索引数、字符串长度）。越界一律 `refuse()` 并
终止当前批次，绝不据此计算偏移或分配。guest 是不可信输入——这不是防恶意，是
防"游戏在崩溃路径上发了半条记录"。

## 7. Host：`gl_executor.js` 的结构

```text
GLWebGPUExecutor
├── 传输层
│   ├── submit(bytes, metadata)          由 bridge 调用，入队
│   ├── executeBatch()                   解析记录、分发、必要时提前 flush
│   └── dispatch(fn, view, offset, size) 217+ 路分发（表驱动，不是 switch）
├── 上下文与共享组
│   ├── contexts: Map<handle, GLContextState>
│   ├── shareGroups: Map<id, {textures, buffers, programs, shaders, lists, renderbuffers}>
│   └── current: GLContextState
├── GL 状态机（GLContextState）
│   ├── 矩阵栈（MODELVIEW 32 深 / PROJECTION 2 深 / TEXTURE[8] 2 深 / COLOR 2 深）
│   ├── 当前顶点属性、启用位、客户端数组指针
│   ├── 光照/材质/雾/texenv/texgen/采样器/像素存储/像素传输
│   ├── 光栅化状态（深度/模板/混合/裁剪/多边形偏移/点线宽/点画）
│   └── 帧缓冲绑定（默认 FBO 或用户 FBO）、draw/read buffer 选择
├── 资源
│   ├── textures  handle -> {target, levels[], gpuTexture, view 缓存, 采样器签名}
│   ├── buffers   handle -> {gpuBuffer, size, usage, 影子副本(按需)}
│   ├── programs  handle -> {vs, fs, wgsl, reflection, uniform 影子, locations}
│   ├── framebuffers / renderbuffers
│   └── queries   handle -> {querySetIndex, 结果, 状态}
├── 帧与通道
│   ├── ensureFrame() / recordOp() / finishFrame(present)
│   ├── beginRenderPassIfNeeded()        延迟开 pass，clear 尽量走 loadOp
│   └── presentTo(swapchainTexture)      翻转 + resolve + sRGB
├── 管线
│   ├── fixedFunctionSignature() / programSignature()
│   ├── pipelineCache / bindGroupCache / samplerCache / moduleCache
│   └── uniformRing（与 d3d9 同一实现）
└── 诊断
    ├── getStats() / refuse() / warnOnce()
    ├── dumpShaders() / dumpPipelineStates() / textureDiagnostic()
    └── blackScreenReport()
```

分发用**表**而不是 `switch`：`const HANDLERS = new Array(256); HANDLERS[GLFN_VIEWPORT] = ...`。
217 路 `switch` 在 V8 上会退化成线性比较链，而这是每帧执行几万次的热点。表还让
"哪些操作码没实现"变成一次数组扫描，直接喂给 COVERAGE.md。

### 7.1 帧结构与 render pass 的开合

GL 没有"帧"的概念，只有 `SwapBuffers`。executor 的帧边界：

- `ensureFrame()` 在第一条产生 GPU 工作的命令上创建 `GPUCommandEncoder`。
- render pass **延迟到第一次 draw 才真正 begin**，因为 `loadOp` 只能在 begin 时
  指定。`glClear` 如果是当前 pass 尚未 begin 时的第一件事，就变成
  `loadOp: "clear"`（零成本）；如果 pass 已经开着，就退化成一次全屏
  quad draw（受 scissor 与 colorMask 约束，语义与 GL 一致）。这是 GL 上最常见
  的模式（`glClear` 在帧首），所以绝大多数帧走的是零成本路径。
- 切换渲染目标（`glBindFramebuffer`、`glDrawBuffer`）结束当前 pass。
- 累计操作数超过阈值（沿用 `d3d9_executor.flushThreshold` 的 16384）提前
  `queue.submit()`，因为后备缓冲是自有纹理，可以在不 present 的情况下提交。
- `wglSwapBuffers` → `finishFrame(present=true)`：结束 pass、提交、blit 到
  swapchain。

### 7.2 与 bridge 的接口

`v86_network_bridge.js` 里 GL 相关的三千行删掉，剩下：

```js
setGLExecutorFromOptions()   // 与 setD3D9ExecutorFromOptions 同构
pushGLPCIBatch(event, bytes) // 裸记录流 -> this.glExecutor.submit(bytes, meta)
```

`V86GL_CTRL_MAKE_CURRENT`/`RELEASE_CURRENT`/`DESTROY_CONTEXT`（`0xFFF0`–`0xFFF2`）
从 bridge 的特判改为 executor 的普通操作码——上下文现在是 executor 的概念。
覆盖层的定位/显隐（`emit_current_surface` 传来的 HWND 与矩形）继续由 bridge 做，
接口与 `d3d9Executor` 的 `onSurface` 回调同构。

## 8. Host：`gl_shader_translator.js`（GLSL 1.20 → WGSL）

### 8.1 前端

- **预处理器**：`#define`（含参数宏、`##` 不需要，GLSL 没有）、`#undef`、
  `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`、`defined()`、`#line`、
  `#error`、`#pragma`（识别并忽略 `optimize`/`debug`，`STDGL invariant(all)`
  要处理）、`#extension`（`GL_ARB_texture_rectangle` 等，未知扩展按
  `require` 报错 / `enable`、`warn`、`disable` 分别处理）、`#version 110/120`。
  内建宏 `__VERSION__`、`__LINE__`、`__FILE__`、`GL_ES`（不定义）。
- **词法/语法**：完整 GLSL 1.20 文法。要点：`attribute`/`varying`/`uniform`/
  `const` 存储限定符、`invariant`、精度限定符（1.20 里可出现但无语义）、
  结构体、数组（1.20 只允许一维、大小为常量表达式）、函数重载与
  `in`/`out`/`inout` 参数、`discard`、`return`、循环与 `break`/`continue`。
- **类型检查**：GLSL 的隐式转换只有 `int → float`（含向量分量级）。WGSL 完全
  没有隐式转换，所以类型检查器必须把每一处隐式转换在 AST 上物化成显式
  `f32()`/`vec3<f32>()`。这是翻译器最容易出静默 bug 的地方，也是
  `gl_shader_translator_test.js` 覆盖最密的部分。
- **常量折叠**：数组大小、`#if` 表达式、`const` 初值需要真正的常量求值器。

### 8.2 兼容内建（这是"实现 GLSL 1.20"与"跑通几个 shader"的分界线）

顶点端可见的兼容内建，全部由固定管线 uniform 块支持：

```text
属性  gl_Vertex gl_Normal gl_Color gl_SecondaryColor gl_FogCoord
      gl_MultiTexCoord0..7
矩阵  gl_ModelViewMatrix gl_ProjectionMatrix gl_ModelViewProjectionMatrix
      gl_TextureMatrix[8] gl_NormalMatrix
      以上各自的 Inverse / Transpose / InverseTranspose 变体（GLSL 1.20 全都有）
状态  gl_LightSource[8]（position/ambient/diffuse/specular/spotDirection/
      spotExponent/spotCutoff/spotCosCutoff/constant|linear|quadraticAttenuation/
      halfVector）
      gl_FrontMaterial gl_BackMaterial gl_LightModel
      gl_FrontLightModelProduct gl_BackLightModelProduct
      gl_FrontLightProduct[8] gl_BackLightProduct[8]
      gl_Fog（color/density/start/end/scale）
      gl_TextureEnvColor[8] gl_ClipPlane[6]
      gl_DepthRange（near/far/diff）
      gl_Point（size/sizeMin/sizeMax/fadeThresholdSize/distanceConstant|Linear|QuadraticAttenuation）
输出  gl_Position gl_PointSize gl_ClipVertex
      gl_FrontColor gl_BackColor gl_FrontSecondaryColor gl_BackSecondaryColor
      gl_TexCoord[8] gl_FogFragCoord
函数  ftransform()
```

片段端：`gl_FragCoord`、`gl_FrontFacing`、`gl_PointCoord`、`gl_Color`、
`gl_SecondaryColor`、`gl_TexCoord[]`、`gl_FogFragCoord`、`gl_FragColor`、
`gl_FragData[]`、`gl_FragDepth`。

**只为被引用到的内建分配 uniform 空间**（活跃性分析），否则每个程序都要拖着一个
上百个 vec4 的状态块。`gl_LightProduct` 这类"派生量"在 CPU 侧算好上传，不在
着色器里现算。

`ftransform()` 必须逐字等价于 `gl_ModelViewProjectionMatrix * gl_Vertex`
且与固定管线路径产生**位相同**的结果（GL 规范要求 `ftransform` 的不变性），
所以固定管线的变换也用同一段生成代码。这条直接决定多趟渲染（Cube 2 的深度预
通道 + 光照通道）会不会出 z-fighting。

### 8.3 内建函数映射

绝大多数一一对应（`abs`/`min`/`max`/`clamp`/`mix`/`step`/`smoothstep`/
`pow`/`exp`/`log`/`sqrt`/`inversesqrt`/`floor`/`ceil`/`fract`/`mod`/
`normalize`/`dot`/`cross`/`length`/`distance`/`reflect`/`refract`/
`faceforward`/`dFdx`/`dFdy`/`fwidth`）。需要写等价实现的：

| GLSL | WGSL |
| --- | --- |
| `mod(x, y)` | `x - y * floor(x / y)`（WGSL 的 `%` 是 C 语义，符号不同） |
| `matrixCompMult` | 逐分量展开 |
| `outerProduct` | 展开（1.20 新增） |
| `transpose` / `inverse` | `transpose` 有；`inverse` GLSL 1.20 没有，不用管 |
| `atan(y, x)` | `atan2(y, x)` |
| `texture2D`/`texture2DProj`/`texture2DLod`/`texture2DProjLod` | `textureSample` / 手动投影除 / `textureSampleLevel` |
| `texture1D*` | 1D 纹理映射为高度 1 的 2D（11.2） |
| `texture3D*` / `textureCube*` | `textureSample`（`texture_3d` / `texture_cube`） |
| `shadow2D`/`shadow2DProj` | `textureSampleCompare`（比较采样器） |
| `noise1..4` | 返回 0（B 类偏差 D-11；GL 规范允许实现返回 0，桌面驱动普遍如此） |
| 关系函数 `lessThan` 等 | WGSL 的 `<` 对向量即逐分量 |
| `any`/`all`/`not` | 直接对应 |

### 8.4 代码生成

- **入口结构**：顶点着色器生成
  `@vertex fn vs_main(in: VSIn) -> VSOut`，片段生成
  `@fragment fn fs_main(in: FSIn) -> FSOut`。GLSL 的全局变量（含
  `varying`/`attribute`）在 WGSL 里变成函数内的 `var<private>` 或结构体字段，
  `main()` 变成一个普通函数，入口函数负责搬入/搬出。这样 GLSL 里"在任意函数里
  写 `gl_Position`"的合法写法不需要特殊处理。
- **uniform 布局**：一个 `struct` + `@group(1) @binding(0) var<uniform>`，
  按 std140 兼容的对齐规则排布（WGSL 的 uniform 地址空间对齐规则与 std140 接近
  但不相同：`vec3` 在 WGSL 里对齐 16 但**步长**也是 16，数组元素在 uniform 地址
  空间里必须 16 字节对齐）。**`float uniform[N]` 数组是最大的坑**：std140 里每个
  元素占 16 字节，WGSL 同样，所以 CPU 侧上传时必须按 16 字节步长展开。翻译器
  产出的反射结构里带每个 uniform 的字节偏移与步长，`glUniform1fv` 按它写入。
- **采样器**：GLSL 的 `sampler2D` 是"纹理+采样器"合体，WGSL 分开。每个采样器
  uniform 生成一对 binding。`glUniform1i(loc, unit)` 改的是"这个采样器绑到哪个
  纹理单元"，是 host 侧的绑定表更新，不进 uniform 缓冲。
- **整数字面量与位运算**：GLSL 1.20 没有位运算符，不用管；但 `int` 是有符号
  32 位，映射到 `i32`。
- **精度**：GLSL 1.20 的精度限定符无语义，全部按 f32。

### 8.5 反射结构

链接产出，供 executor 与 guest 查询使用：

```js
{
  ok, wgslVertex, wgslFragment, log,
  attributes: [{name, location, type}],           // glGetActiveAttrib / glGetAttribLocation
  uniforms:   [{name, location, type, arraySize, offset, stride}], // glGetActiveUniform
  samplers:   [{name, location, unitDefault, dimension, shadow}],
  varyings:   [{name, slot, components}],         // 4.11 的打包结果
  builtins:   {usedVertexBuiltins, usedFragmentBuiltins, writesPointSize, writesFragDepth, discards},
  stats:      {nonUniformSamples, hoistedSamples}
}
```

`location` 的编号规则必须与桌面 GL 一致的地方：数组 uniform 的
`loc(name[i]) == loc(name[0]) + i`，`glGetUniformLocation("name[0]")` 与
`glGetUniformLocation("name")` 等价。Cube 2 依赖这两条。

### 8.6 缓存

翻译结果按 `(vsSource, fsSource, bindAttribLocations, 编译器版本号)` 的哈希缓存，
命中即跳过整条前端。缓存持久化到 IndexedDB（键前缀 `glwg.shader-cache.`），
与 `d3d9_executor` 用同一套存取代码。Cube 2 首次进图会链接上百个程序，第二次
应当接近零成本。翻译放在 worker 里（`gl_shader_worker.js`），因为 GLSL 前端比
D3D9 字节码翻译重得多，主线程停顿会直接表现为 v86 卡顿。

### 8.7 与固定管线共用的尾部

无论 GLSL 路径还是固定管线路径，顶点着色器的最后都追加同一段：

```wgsl
out.position = clip;
out.position.z = (out.position.z + out.position.w) * 0.5;  // GL [-w,w] -> WGPU [0,w]
out.position.y = -out.position.y;                          // 4.3
```

裁剪平面同理：两条路径都把 `dot(gl_ClipVertex, plane[i])` 作为 varying 传给片段
端，片段端统一 `if (any(clipDist < 0)) { discard; }`（WebGPU 没有
`clip_distances`，除非 `clip-distances` feature 可用——可用时走真的裁剪，见
16 章 D-05）。

## 9. Host：`gl_arb_program.js`（ARB assembly → WGSL）

`GL_ARB_vertex_program` / `GL_ARB_fragment_program` 是寄存器式汇编，与 D3D9 的
shader model 1.x/2.0 结构上极其接近：有限的临时/输入/输出/参数寄存器、swizzle、
写掩码、源修饰符、饱和输出。**`d3d9_shader_pipeline.js` 的代码生成骨架
（寄存器分配、swizzle/掩码展开、`OP` 表驱动）可以直接借用**，需要重写的是
解析（文本 vs 字节码）和指令语义表。

要点：

- 程序参数：`program.env[0..N]`、`program.local[0..N]` 是**每个程序各自的
  命名空间**，顶点与片段程序在同一索引上可以是不同的值，因此两级各占一个
  binding（顶点 `@group(1) @binding(1)`、片段 `@group(1) @binding(2)`）；
  合并成一个 block 会让其中一级读到另一级的参数。以及状态绑定
  `state.matrix.*`（modelview/projection/mvp/texture[n]，含 `.inverse`/
  `.transpose`/`.invtrans` 与行区间选择）、`state.light[n].*`、
  `state.material.*`、`state.fog.*`、`state.texenv[n].color`、
  `state.clip[n].plane`、`state.depth.range`——与 8.2 的兼容内建共用同一份
  uniform 状态，不再实现第二遍。`NATIVE`/`ENV`/`LOCAL` 的计数上限沿用现在
  声明的 28 个片段参数（`openglproxy/README.md` 解释过为什么是 28 而不是 ARB
  最低要求的 24）。
- 顶点程序输出 `result.position`/`result.color`/`result.texcoord[n]`/
  `result.fogcoord`/`result.pointsize`；片段程序输出 `result.color`/
  `result.depth`。
- 纹理指令 `TEX`/`TXP`/`TXB`，目标由 `texture[n]` 与 `2D`/`3D`/`CUBE`/`RECT`
  后缀决定。
- `ARL`/地址寄存器与相对寻址 `program.env[A0.x + 3]`：WGSL 支持 uniform 数组的
  动态索引，直接映射。
- `KIL`（片段丢弃）→ `discard`。
- 选项 `ARB_position_invariant`：顶点程序不写 `result.position`，由固定管线
  变换补上——必须与 `ftransform()` 用同一段代码（8.2 的不变性理由）。

## 10. 固定管线状态 → WebGPU / WGSL 映射

### 10.1 直接映射

| GL | WebGPU |
| --- | --- |
| `glDepthFunc` | `depthStencil.depthCompare` |
| `glDepthMask` | `depthStencil.depthWriteEnabled` |
| `glCullFace` / `glFrontFace` / `GL_CULL_FACE` | `primitive.cullMode` / `frontFace`（注意 4.3 的绕向反转） |
| `glBlendFuncSeparate` / `glBlendEquationSeparate` | `blend.color` / `blend.alpha` |
| `glBlendColor` | `pass.setBlendConstant()` |
| `glColorMask` | `target.writeMask` |
| `glScissor` / `GL_SCISSOR_TEST` | `pass.setScissorRect()`（关闭时设为全表面） |
| `glViewport` / `glDepthRange` | `pass.setViewport()` |
| `glPolygonOffset` | `depthStencil.depthBias` / `depthBiasSlopeScale`（进 pipeline 签名） |
| `glStencilFuncSeparate` / `glStencilOpSeparate` | `depthStencil.stencilFront` / `stencilBack` + `pass.setStencilReference()` |
| `glSampleCoverage` / `GL_SAMPLE_ALPHA_TO_COVERAGE` | `multisample.mask` / `alphaToCoverageEnabled` |
| `GL_DEPTH_CLAMP` | `primitive.unclippedDepth`（需 `depth-clip-control`） |

### 10.2 生成到 WGSL

| GL | 做法 |
| --- | --- |
| 模型视图/投影/纹理矩阵栈 | uniform，CPU 侧维护栈 |
| 8 光源完整方程 + 双面 + 局部视点 + 分离高光 | 顶点着色器生成，按签名裁剪掉未启用的光源 |
| `glColorMaterial` | 顶点着色器里用当前颜色替换材质的对应项 |
| `GL_NORMALIZE` / `GL_RESCALE_NORMAL` | 顶点着色器 |
| 雾（LINEAR/EXP/EXP2，`GL_FOG_COORD_SRC`） | 顶点算坐标，片段算因子并混合 |
| `glTexGen`（OBJECT_LINEAR / EYE_LINEAR / SPHERE_MAP / REFLECTION_MAP / NORMAL_MAP） | 顶点着色器 |
| `GL_TEXTURE_ENV`（REPLACE/MODULATE/DECAL/BLEND/ADD/COMBINE + DOT3 + crossbar + RGB_SCALE） | 片段着色器逐单元求值 |
| `glAlphaFunc` / `GL_ALPHA_TEST` | 片段 `discard` |
| `glClipPlane`（6 个） | varying + 片段 `discard`（或真裁剪，见 D-05） |
| `GL_POINT_SMOOTH` / 点大小衰减 / `GL_POINT_SPRITE` | 点展开成四边形（10.3） |
| `glShadeModel(GL_FLAT)` | varying 上 `@interpolate(flat)` + 4.6 的顶点排序 |

### 10.3 需要几何展开的

| GL 特性 | 方案 |
| --- | --- |
| 点大小 > 1 / 点精灵 | 每个点展开成两个三角形，尺寸由顶点算出的 `gl_PointSize` 决定；`gl_PointCoord` 由展开的角点提供。`d3d9_executor` 的点精灵实现是同一手法 |
| 线宽 > 1 | 每条线展开成一个四边形（屏幕空间外扩半宽）；端点按 GL 的方形端点规则截断 |
| `glPolygonMode(GL_LINE)` | 三角形索引展开成三条线；`GL_POINT` 展开成点。注意 GL 的边标志（`glEdgeFlag`）会隐藏内部边，guest 已经有 edge flag 状态，随顶点带过来 |
| `glLineStipple` | 顶点端累计沿线的屏幕空间距离作为 varying，片段端按 `pattern`/`factor` 查位并 `discard` |
| `glPolygonStipple` | 32×32 位掩码上传成 R8 纹理，片段端按 `gl_FragCoord` 取模查表并 `discard` |
| 累积缓冲 | 一张 `rgba16float` 全屏纹理；`glAccum` 的 5 个操作各是一次全屏 pass（`GL_ACCUM`/`GL_LOAD`/`GL_RETURN`/`GL_MULT`/`GL_ADD`） |
| `glDrawPixels` / `glBitmap` / `glCopyPixels` | 像素块上传成临时纹理，按当前光栅位置画一个屏幕对齐的四边形；`glPixelZoom` 决定缩放；`glBitmap` 用 R8 掩码 + `discard` 并推进光栅位置 |

### 10.4 无法精确表达的

`glLogicOp`：WebGPU 没有逻辑操作混合。方案：`GL_COPY`（默认）无需处理；
其余 15 种在启用 `GL_COLOR_LOGIC_OP` 时，把当前颜色附件作为纹理绑进片段着色器
（需要先结束 pass 做一次拷贝，因为 WebGPU 不允许同时读写同一纹理），在着色器
里做整数逻辑运算。代价是每个受影响的 draw 一次全表面拷贝。**只在真的启用时
才付这个代价**，并计数上报。橡皮筋选框（`GL_XOR`）是唯一常见用法，一帧一两次。
见 D-01。

分离的双面模板**掩码**：WebGPU 的 `stencilReadMask`/`stencilWriteMask` 前后面
共用，GL 的 `glStencilMaskSeparate`/`glStencilFuncSeparate` 允许不同。前后面
掩码不同时取前面的并计数上报。见 D-02。

## 11. 纹理与格式

### 11.1 格式矩阵

`glTexImage*` 的 `internalFormat` 在 GL 里既有"未定尺寸"的（`GL_RGB`、`GL_RGBA`、
`3`、`4`）也有定尺寸的（`GL_RGBA8`、`GL_RGB5_A1`…）。host 侧一张表把
(internalFormat, format, type) 三元组映射到 `GPUTextureFormat` + 一个行转换器：

| GL internalFormat | GPU 格式 | 说明 |
| --- | --- | --- |
| `GL_RGBA` / `GL_RGBA8` / `4` | `rgba8unorm` | 基准 |
| `GL_RGB` / `GL_RGB8` / `3` | `rgba8unorm` | 补 alpha=1；GL 的采样语义要求 alpha 读作 1 |
| `GL_RGB5_A1` / `GL_RGBA4` / `GL_RGB565` | `rgba8unorm` | 上传时展开；GL 允许实现选更高精度 |
| `GL_LUMINANCE` / `GL_ALPHA` / `GL_LUMINANCE_ALPHA` / `GL_INTENSITY` | `rgba8unorm` | 上传时按 GL 的通道复制规则展开（L→rrr1、A→000a、LA→rrra、I→rrrr）。**这四种的展开规则写错是最常见的静默色差来源**，专项测试 |
| `GL_BGRA` / `GL_UNSIGNED_INT_8_8_8_8_REV` 等打包类型 | `rgba8unorm` | 上传时重排；`bgra8unorm` 只在恰好匹配时直用 |
| `GL_COMPRESSED_RGB(A)_S3TC_DXT1/3/5` | `bc1/2/3-rgba-unorm` | adapter 有 `texture-compression-bc` 时直传；否则 CPU 解码成 `rgba8unorm`（现有代码可复用） |
| `GL_DEPTH_COMPONENT16/24/32` | `depth16unorm` / `depth24plus` / `depth32float` | 深度纹理 |
| `GL_DEPTH_COMPONENT` + `GL_TEXTURE_COMPARE_MODE` | 同上 + 比较采样器 | 阴影贴图 |
| `GL_RGBA16F` / `GL_RGBA32F`（`GL_ARB_texture_float`） | `rgba16float` / `rgba32float` | 过滤需 `float32-filterable` |
| `GL_COLOR_INDEX8_EXT`（调色板纹理） | `r8uint` + 调色板 uniform | 片段端查表，与 D3D9 的 P8 路径同构 |
| `GL_SRGB` / `GL_SRGB8_ALPHA8`（1.4 的 `GL_EXT_texture_sRGB`） | `rgba8unorm-srgb` | |

`glPixelStorei` 的 `UNPACK_ROW_LENGTH`/`SKIP_PIXELS`/`SKIP_ROWS`/`ALIGNMENT`/
`SWAP_BYTES`/`LSB_FIRST` 在上传前的行打包里处理（guest 已经解析 PBO 偏移，行
参数在 host 侧应用）。`writeTexture` 要求 `bytesPerRow` 256 对齐，所以几乎所有
上传都要重新打包一遍行——`d3d9_executor` 已经有这段代码。

### 11.2 目标

- `GL_TEXTURE_1D` → 高度 1 的 `texture_2d`，`texture1D()` 采样时 t=0.5。
- `GL_TEXTURE_2D` → `texture_2d`。
- `GL_TEXTURE_3D` → `texture_3d`。
- `GL_TEXTURE_CUBE_MAP` → 6 层的 `texture_cube`；六个面的上传顺序与 GL 枚举
  一致。**注意 4.3 的 Y 翻转不适用于立方体贴图的面朝向**：立方体的面方向由
  规范定死，与帧缓冲朝向无关；渲染到立方体面时才需要按面单独处理绕向。
- `GL_TEXTURE_RECTANGLE`（若 M0 发现有人用）→ `texture_2d` + 非归一化坐标在
  着色器里除以尺寸。

### 11.3 采样器

`glTexParameter` 的 min/mag filter、wrap S/T/R、LOD 范围（`GL_TEXTURE_MIN_LOD`/
`MAX_LOD`）、`GL_TEXTURE_LOD_BIAS`、`GL_TEXTURE_BASE_LEVEL`/`MAX_LEVEL`、
各向异性、比较模式。映射注意点：

- `GL_CLAMP`（老的"钳到边框"）与 `GL_CLAMP_TO_BORDER`：WebGPU 只有
  clamp-to-edge / repeat / mirror-repeat。用 clamp-to-edge + 片段端"坐标出界则
  取边框色"的显式判断实现（`d3d9_executor` 的 BORDER 寻址是同一手法）。
  这使采样器状态进入**着色器签名**，不只是采样器签名。见 D-03。
- `GL_MIRRORED_REPEAT` → `mirror-repeat`，直通。
- `GL_TEXTURE_BASE_LEVEL`/`MAX_LEVEL` → `createView({baseMipLevel, mipLevelCount})`，
  按 (纹理, base, max) 缓存 view。
- `GL_TEXTURE_LOD_BIAS` 与 `GL_TEXTURE_ENV` 的 `GL_TEXTURE_LOD_BIAS_EXT` 相加，
  在采样时用 `textureSampleBias`。
- 各向异性要求 min/mag/mip 三个过滤器都是 linear，否则 WebGPU 拒绝创建；
  不满足时静默降到 1 并计数。
- `glGenerateMipmap` → 一串全屏 blit pass（WebGPU 没有内建 mipmap 生成）。
  盒式滤波，按 GL 规范的 2×2 平均。

### 11.4 纹理完备性

GL 的"纹理完备性"（mip 链完整、尺寸递减正确、格式一致）在 GL 里决定采样是否
返回 (0,0,0,1)。host 侧必须实现这条判断，因为很多游戏靠"未完备纹理不采样"来
偷懒。不完备时绑定 1×1 的黑色回退纹理（`d3d9_executor` 的 `fallbackTexture`
同一位置，颜色不同：D3D9 用白，GL 规范要求黑+alpha 1）。

## 12. 帧缓冲对象

`GL_EXT_framebuffer_object` / `GL_ARB_framebuffer_object` 的映射：

- FBO 对象 = 一组附件引用（颜色 0..N、深度、模板、深度模板）。绑定 FBO 不产生
  GPU 工作，只改"下一个 render pass 的附件描述"。
- `glFramebufferTexture2D` / `glFramebufferRenderbuffer` → 记录附件；
  `glCheckFramebufferStatus` 在 host 侧按 GL 的完备性规则同步回答（尺寸一致、
  格式可渲染、至少一个附件、draw buffer 指向存在的附件）。
- Renderbuffer → 一张 `RENDER_ATTACHMENT` 用途的纹理（WebGPU 没有独立的
  renderbuffer 概念）；`glRenderbufferStorageMultisample` → `sampleCount`。
- `glDrawBuffers` → render pass 的 `colorAttachments` 数组顺序；
  `GL_NONE` 的槽位在 WebGPU 里用 `null` 表示（WebGPU 允许 sparse 附件）。
- `glBlitFramebuffer` → 同格式同尺寸时 `copyTextureToTexture`；需要缩放或格式
  转换时一次全屏 blit pass；带 `GL_LINEAR` 时用线性采样。深度/模板的 blit 只
  在同格式同尺寸时支持（WebGPU 无法着色器写深度做缩放 blit），否则拒绝并计数。
- 默认帧缓冲（FBO 0）= 当前上下文的后备缓冲 + 深度模板。`glDrawBuffer(GL_BACK)` /
  `GL_FRONT` / `GL_FRONT_AND_BACK`：我们只有后备缓冲，`GL_FRONT` 的写入被拒绝
  并计数（D-08）。
- MSAA：默认帧缓冲的采样数由 WGL pixel format 决定（现在恒为 1，M6 加 4x）；
  FBO 的多重采样附件在 pass 里配 `resolveTarget`。WebGPU 只定义 1 和 4，
  其它请求向下取到 4。

## 13. 性能预算与风险

### 13.1 真实瓶颈是记录数与 draw call 数，不是 GPU

`opengl-call-profile.md`（M0 产出）会给出准确数字，先给量级估计：

| 场景 | 每帧记录数 | 主导成本 |
| --- | --- | --- |
| 半条命，一张地图 | 2–5 万（立即模式为主） | guest 侧 `memcpy` 进竞技场 + host 侧记录解析 |
| Cube 2，一张地图 | 3–8 千（VBO + GLSL） | draw call 数 + bind group 创建 |
| GL demo | < 1 千 | 可忽略 |

对策，按收益排序：

1. **立即模式批量记录**（6.4）：记录数降两个数量级。
2. **连续兼容批次合并**（4.7）：draw call 降一到两个数量级。
3. **VBO 提升**（6.5）：消除每帧重传。
4. **bind group 缓存**按 (纹理 view 集合, 采样器集合, uniform chunk) 键，
   与 `d3d9_executor` 同一实现（它已经解决过"瞬时 buffer 破坏缓存"的问题）。
5. **render bundle**：状态签名连续不变的一串 draw 录成 `GPURenderBundle`，
   跨帧复用。GL 的显示列表是天然的 bundle 边界——`glCallList` 重放同一串命令
   且中间没有 uniform 变化时，直接重放 bundle。M7 的优化项。

### 13.2 同步查询是第二个悬崖

每一次走响应区的查询都让 guest 自旋，等于 VM 停摆。M0 的 (a) 数据决定要不要做
额外优化：

- `glReadPixels` 在半条命里只在截图时用；在 Cube 2 里不用。低风险。
- 遮挡查询在 Cube 2 里每帧几百个，但已经是"一次批量解决"的设计（现有
  `GLFN_QUERY_OBJECT_BATCH`），保持批量即可。
- `glFinish` 若被每帧调用（某些引擎用它做帧率限制）会是灾难。发现时的对策：
  按 GL 规范允许的最弱语义应答——`glFinish` 只需保证之前的命令"完成"，而我们
  的命令在 `queue.submit()` 后对 guest 而言不可观测，所以可以在 submit 后立即
  应答而不等 `onSubmittedWorkDone()`。**默认走严格语义，发现性能问题时用
  一个明确命名的选项放宽，并登记为偏差。**

### 13.3 风险清单

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| GLSL 编译器规模失控 | M4 无限延期 | M0 就把运行时语料落盘；编译器按语料驱动开发，naga 是硬门槛；先做 Cube 2 用到的子集，`COVERAGE.md` 记录未实现的语法 |
| 非一致控制流采样（4.10）近似出画面差异 | Cube 2 的水/凹凸着色器 | 计数上报 + 逐个截图对比；提升策略覆盖不了时优先改用显式导数 |
| Y 翻转/绕向漏改一处 | 整类几何消失或镜像 | 绕向只允许在一个函数里出现；M1 就加"绕向 + 剔除"的四象限专项测试 |
| 固定管线签名漏字段 | "改了状态画面没变"的幽灵 bug | 签名函数逐字段注释；M2 加一个遍历所有状态位、逐个改动并断言签名变化的测试 |
| 多上下文共享组做错 | WineD3D 式的探测上下文破坏主上下文 | 4.13 的模型 + `gl_wgl_thread_current_test.exe` 扩展 |
| 与 D3D 路径抢 canvas | 黑屏且沉默 | 4.2 的 presenter 令牌 + 明确 error |
| 性能不如现管线 | 迁移失去意义 | A/B 开关（5.4）保留到 M7；每个里程碑跑同一组帧率基准 |
| 并发会话改同一批文件 | 审计结论过期 | 动手前查 `git status` 与文件 mtime |

## 14. 里程碑

每个里程碑的退出条件都包含"COVERAGE.md 对应行从空缺变为已实现或已登记偏差"。

### M0：基线与追踪

见第 5 章。产出 `COVERAGE.md`、`opengl-call-profile.md`、运行时 GLSL 语料、
A/B 开关。**不写渲染代码。**

### M1：骨架跑通一个三角形

- `webgpu_host.js`：device/context 单一所有者；`d3d9_executor.js` 改为从它取
  device，不再自己 `configure()`；`d3d9_webgpu_executor_test.js` 与
  `v86_network_bridge_d3d9_route_test.js` 必须仍然全绿（这是"不弄坏 D3D"的
  硬门槛）。
- `gl_executor.js`：上下文创建/切换/销毁、后备缓冲、present（含 4.3 的翻转
  blit）、`glViewport`/`glScissor`/`glClear*`/`glEnable`/`glDisable` 的状态机、
  立即模式累加器、最小固定管线 WGSL（位置 + 顶点颜色 + 深度测试）。
- 图元展开（4.6）与绕向（4.3）完整实现——这两件事越早定死越好。
- bridge：`glBackend: "webgpu"` 路由到新 executor，`#v86gl_canvas` 从
  `game.html` 移除。

退出条件：`gl_triangle_test.exe`、`gl_test_depth_clear_poison.exe`、
`gl_test_swap_postclear.exe` 在 XP guest 里画面正确；新增的绕向/剔除四象限
测试通过；D3D9 的全部既有测试仍绿。

### M2：固定管线完整

- 矩阵栈全套（含 `glLoadMatrix`/`glMultMatrix`/转置变体/`glPushMatrix` 深度限制）。
- 光照：8 光源、方向/点/聚光、双面、局部视点、分离高光、`glColorMaterial`、
  法线归一化/重缩放。
- 雾：三种模式、`GL_FOG_COORD_SRC`。
- 纹理环境：`REPLACE`/`MODULATE`/`DECAL`/`BLEND`/`ADD`/`COMBINE`（RGB/Alpha
  双通道、全部 operand、`RGB_SCALE`/`ALPHA_SCALE`、`DOT3_RGB`/`DOT3_RGBA`、
  crossbar `GL_TEXTURE<n>` 源）。
- `glTexGen` 五种模式 + 纹理矩阵。
- alpha test、裁剪平面、`glShadeModel(GL_FLAT)` 的 provoking vertex。
- 全部光栅化状态（10.1 的表）。
- 客户端数组与 VBO 提升（6.5）、立即模式批量记录（6.4）、连续批次合并（4.7）。
- 显示列表重放（guest 已录制，host 只需正确执行）。

退出条件：`gl_rotate_cube_test.exe`、`gl_client_arrays_test.exe`、
`gl_fog_material_test.exe`、`gl_blend_ui_test.exe`、
`gl_test_blend_alpha_fade.exe` 正确；**半条命与反恐精英能进图并正常游玩**；
帧率不低于 gl4es 路径；A/B 开关默认翻转到 `webgpu`。

### M3：纹理与帧缓冲完整

- 第 11 章的完整格式矩阵、四种目标、完整采样器状态、纹理完备性规则、
  `glGenerateMipmap`、`glCopyTexImage*`/`glCopyTexSubImage*`（含 3D）。
- 调色板纹理（`GL_COLOR_INDEX8_EXT`）。
- 第 12 章的 FBO/renderbuffer/MRT/blit。
- 压缩纹理直传（有 BC feature 时）与 CPU 解码回退。

退出条件：`gl_query_multitexture_test.exe` 正确；
`copy_tex_sub_image_3d_browser_test.html`、`fbo_browser_test.html`、
`fbo_blit_browser_test.html`、`volume_texture_browser_test.html` 改写到新
executor 后全绿；半条命的多重纹理/细节纹理/水面与截图正确。

### M4：GLSL 1.20 编译器

- 第 8 章的完整前端与代码生成，含全部兼容内建、varying 打包、反射、
  非一致控制流处理、IndexedDB 缓存、worker 化。
- `glUniform*` 的全部重载与数组语义、`glBindAttribLocation`、
  `glGetActiveUniform`/`glGetActiveAttrib`、`glValidateProgram`。

退出条件：`cube2_glsl_direct_corpus_r6889.js` 的 38 对 + M0 采集的运行时语料
**全部**产出 naga 接受的 WGSL；`gl2_program_browser_test.html` 与
`cube2_glsl_browser_test.html` 改写后全绿；**Cube 2 能进图并正确渲染世界与模型**。

### M5：ARB 程序、查询、回读

- 第 9 章的 ARB vertex/fragment program 翻译器。
- 响应区协议（6.2/6.3）：`glReadPixels`、遮挡查询批量解决、`glFinish`。
- guest 侧自旋与心跳超时。

退出条件：`arb_program_browser_test.html` 改写后全绿；
`v86_network_bridge_readback_test.js` 的等价测试通过；Cube 2 的遮挡剔除生效
（关闭/开启的帧率差异可观测）且 ARB 路径可用；半条命的 `F5` 截图正确。

### M6：GL 1.x 的长尾

- 累积缓冲、`glDrawPixels`/`glBitmap`/`glCopyPixels`/`glPixelZoom`/
  `glRasterPos*`/`glWindowPos*`。
- 线型点画、多边形点画、`glPolygonMode` 与边标志、宽线、点大小衰减与点精灵。
- `glLogicOp`（10.4 的方案）。
- 默认帧缓冲的 4x MSAA 与 WGL pixel format 的多重采样枚举。
- `GL_SELECT`/`GL_FEEDBACK` 的端到端验证（guest 侧已有实现，但依赖 host 报告的
  视口与矩阵一致性）。

退出条件：每一项一个专项 `sample/gl_*_test.exe` 并通过；`COVERAGE.md` 无 A 类
空缺。

### M7：收敛

- 删除 `libglwasm/`、`Gl4esRenderer`、`callRenderer` 分发、A/B 开关、
  `gl4es` 外部依赖与构建脚本；`game.html`/`app.js` 清理。
- v86 save/load：`stateJournal` 重放到新 executor（6.1 已论证记录格式不变，
  这里是端到端验证）；同时补上 `gl_executor.serializeState()`/`restoreState()`
  用于 host GPU 侧资源的快照——**不要重复 D3D9 路径把这件事漏掉的错误**（17 章）。
- 性能：render bundle、pipeline 预热、着色器缓存命中率；与 M0 基线对比出表。
- 能力真实性复核（4.14）：逐条确认声明的扩展背后有实现。
- `gl-webgpu/README.md` 的偏差清单定稿。

## 15. 测试策略

四层，与 D3D9 路径同构。

### 15.1 Node 单元测试（`glbridge/tests/`，`node xxx_test.js`）

| 文件 | 覆盖 |
| --- | --- |
| `gl_shader_translator_test.js` | GLSL 前端：预处理器、类型检查与隐式转换、重载决议、常量折叠、兼容内建的 uniform 布局、varying 打包、反射（location 编号规则） |
| `gl_shader_wgsl_validation_test.js` | 全部语料过真 `naga`（无 naga 时 skip，与 D3D9 同一模式）。**CI 硬门槛** |
| `gl_fixed_function_wgsl_test.js` | 固定管线签名 → WGSL 的结构断言 + naga 验证；texenv/combine 的真值表逐条 |
| `gl_arb_program_test.js` | ARB 汇编解析与代码生成 |
| `gl_primitive_expansion_test.js` | QUADS/POLYGON/FAN/LINE_LOOP 的索引展开与 provoking vertex |
| `gl_state_signature_test.js` | 遍历每个状态位，断言改动后 pipeline 签名变化（4.4 的漏字段防线） |
| `gl_format_matrix_test.js` | 11.1 的每个三元组：通道展开规则、行打包、256 对齐 |
| `gl_executor_test.js` | 用假 `GPUDevice` 驱动 executor：资源生命周期、上下文/共享组、FBO 完备性、拒绝路径 |
| `gl_protocol_consistency_test.js` | `v86gl_ioctl.h` 的响应区常量与 JS 侧常量一致 |
| `v86_network_bridge_gl_route_test.js` | 裸 GL 记录流路由到 gl executor，D3D 信封不受影响 |
| `gl_perf_test.js` | 记录解析吞吐、pipeline 缓存命中率的回归门槛 |

### 15.2 浏览器测试（`*_browser_test.html`）

现有的 `fbo_browser_test.html`、`fbo_blit_browser_test.html`、
`copy_tex_sub_image_3d_browser_test.html`、`volume_texture_browser_test.html`、
`arb_program_browser_test.html`、`gl2_program_browser_test.html`、
`capabilities_browser_test.html`、`explicit_present_browser_test.html`、
`cube2_*_browser_test.html` 全部改写到新 executor。它们现在断言的是 gl4es 的
行为，改写时**逐条确认断言的是 GL 规范要求的行为**，而不是把 gl4es 的近似固化
成新的基准。`shaderconv_webgl_builtin_browser_test.html` 直接删除（shaderconv
随 gl4es 退役）。

新增：`gl_texenv_browser_test.html`（combine 真值表的像素级验证）、
`gl_raster_ops_browser_test.html`（点画/宽线/PolygonMode/LogicOp）、
`gl_accum_browser_test.html`。

### 15.3 guest 内样例（`glbridge/sample/gl_*_test.c`）

现有 10 个保留并全部在每个里程碑复跑。新增（按里程碑）：
`gl_winding_cull_test.c`（M1）、`gl_flat_shading_test.c`（M2）、
`gl_texenv_combine_test.c`（M2）、`gl_texgen_test.c`（M2）、
`gl_lighting_two_side_test.c`（M2）、`gl_texture_formats_test.c`（M3）、
`gl_fbo_mrt_test.c`（M3）、`gl_glsl_program_test.c`（M4）、
`gl_occlusion_query_test.c`（M5）、`gl_readpixels_test.c`（M5）、
`gl_accum_stipple_test.c`（M6）、`gl_share_lists_test.c`（M7）。

构建脚本比照 `sample/build_d3d8_samples.sh`：一条命令构建全部并拒绝
MSVCRT/UCRT 导入。

### 15.4 真实游戏

每个里程碑结束时按 M0 的同一段脚本跑半条命、反恐精英、Cube 2，截同样的帧，
与 A/B 开关另一侧的截图对比。差异逐一归因：要么是 bug，要么进偏差清单。
**"看起来差不多"不算通过**——D3D8 迁移的教训是静态验证再充分也不能替代在
guest 里跑一次（见 17 章）。

## 16. 已知偏差清单（随实现同步更新，权威副本在 `gl-webgpu/README.md`）

| 编号 | 特性 | 偏差 | 何时会看出来 |
| --- | --- | --- | --- |
| D-01 | `glLogicOp` | WebGPU 无逻辑混合。启用时每个 draw 前拷贝一次颜色附件，在片段着色器里做整数运算。`GL_COPY` 零成本 | 大量图元开着 `GL_COLOR_LOGIC_OP` 时性能显著下降；结果本身正确 |
| D-02 | 分离双面模板掩码 | `stencilReadMask`/`WriteMask` 在 WebGPU 前后面共用，取前面值 | 只有前后面用不同掩码的算法（少见的模板阴影变体）会错 |
| D-03 | `GL_CLAMP` / `GL_CLAMP_TO_BORDER` | 用 clamp-to-edge + 片段端边框色判断模拟；使采样器状态进入着色器签名 | 结果正确，但增加着色器变体数 |
| D-04 | 线宽 / `GL_LINE_SMOOTH` / `GL_POLYGON_SMOOTH` | 宽线用四边形展开，端点按方形规则；平滑（抗锯齿）不实现，`glHint` 忽略 | 宽线的斜接与端点细节与桌面驱动不同；平滑线/多边形边缘是硬边 |
| D-05 | 用户裁剪平面 | 默认用片段 `discard` 模拟。有 `clip-distances` feature 时走真裁剪 | `discard` 版本不裁剪几何，只丢片段：深度预通道 + 裁剪平面的组合可能有差异；性能略低 |
| D-06 | 多重采样 | WebGPU 只定义 1 和 4；`glSampleCoverage(value)` 近似成 `multisample.mask` 的位数 | 请求 2x/8x 时得到 4x；覆盖值的亚采样分布与硬件不同 |
| D-07 | 遮挡查询样本数 | WebGPU 的语义是"是否有样本通过"，可见时报告一个饱和的大样本数 | 用样本数做阈值判断的算法（Cube 2 用的是可见性）会得到饱和值 |
| D-08 | `glDrawBuffer(GL_FRONT)` | 我们只有后备缓冲，前缓冲写入被拒绝并计数 | 直接画到前缓冲的老式代码（调试用）无输出 |
| D-09 | 累积缓冲精度 | 用 `rgba16float` 实现，GL 规范的最低要求是 16 位定点 | 极多次累加（运动模糊 64 采样以上）的舍入与桌面不同 |
| D-10 | 非一致控制流的隐式 LOD 采样 | 无法提升时改用显式导数；循环内改用 LOD 0 | 循环内对带 mipmap 纹理的条件采样会更锐利/有走样 |
| D-11 | `noise1..4()` | 返回 0（桌面驱动普遍如此，GL 规范允许） | 依赖 GLSL 噪声的着色器（罕见）失效 |
| D-12 | `GL_MAX_VARYING_FLOATS` | 报告 16 个 vec4；超出时链接失败并给出明确日志 | 超过 16 个活跃 varying 的着色器无法链接（桌面下限是 8 vec4，我们更宽松） |
| D-13 | `glFinish` | 严格语义默认开启（等 `onSubmittedWorkDone`）；发现性能问题时可用选项放宽到 submit 即返回 | 放宽后，用 `glFinish` 做 CPU/GPU 同步计时的代码得到偏小的耗时 |
| D-14 | 颜色索引渲染模式 | 不实现（2.2） | 使用 `glIndex*` 的 1996 年代码无输出。目标游戏无一使用 |
| D-15 | `glDrawPixels` / `glBitmap` / `glCopyPixels` | 当前拒绝并计数；纹理四边形实现是 M6 项 | 用像素矩形画的加载画面与文字不出现 |
| D-16 | 线型点画 / 多边形点画 | 图案已存储并可回读，但尚未在着色器里应用 | 点画线与点画多边形画成实心 |
| D-17 | `glPolygonMode(GL_LINE\|GL_POINT)` | 尚未展开成线/点图元 | 线框模式画成实心 |
| D-18 | 一个阶段用 ARB 程序、另一个用固定管线 | 拒绝：两者不共享 varying 布局 | 只启用 `GL_VERTEX_PROGRAM_ARB` 的程序不出画面，但会明确报错 |

编号一经分配不再复用；实现掉某条偏差时保留编号并标注"已消除于 Mx"。
权威副本在 `glbridge/gl-webgpu/README.md`，本表与它同步。

## 17. 与既有路径的关系

### 17.1 四个 guest 前端，一个 GPU 后端

迁移完成后：

```text
d3d8.dll   ─┐
d3d9.dll   ─┼─> D9WG (0xFFE1) ─> d3d9_executor.js ─┐
ddraw.dll  ─┘                                      ├─> webgpu_host.js ─> #d3d_webgpu_canvas
opengl32.dll ─> 裸 GL 记录流 ────> gl_executor.js ──┘
```

`gl_executor.js` **不是**第五个后端：它与 `d3d9_executor.js` 共用
`webgpu_host.js` 的 device/context、uniform ring、bind group/sampler 缓存、
着色器持久缓存、设备丢失恢复、格式转换与回读打包。两者是同级的兄弟，各自负责
一族 API 的语义。

为什么 GL 不像 D3D8 那样"翻译成 D9WG"：D3D8 是 D3D9 的语义子集（DXVK 已经证明
过），而 OpenGL 不是。GL 的纹理环境组合、光照模型、立即模式、显示列表、
`glPushAttrib`、GLSL 都没有 D3D9 对应物；硬翻会在中间层引入一整套有损映射，
而下面那层最终还是要重新展开成 WGSL。真正值得共用的是"WGSL 之下"的东西，
而那正是 `webgpu_host.js` 与 `gpu_common.js` 的边界。

### 17.2 从 D3D 迁移里继承的三条教训

1. **静态验证不能替代在 guest 里跑一次。** D3D8 的迁移做完了全部 JS 套件、
   178 个 shader 的 naga 验证，然后"从未对真实游戏跑过"成了第一优先级的遗留项。
   本方案的每个里程碑退出条件都包含一次 guest 内的真实运行。
2. **save/restore 会被迁移悄悄弄丢。** `d3d9_executor.js` 至今没有
   `serializeState`/`restoreState`，`v86_network_bridge.js` 用 `typeof` 检查
   保护，于是它静默失败。GL 路径**现在有**可用的状态日志重放，M7 必须端到端
   验证它还在，并补上 host GPU 资源的快照。
3. **不要声明没有实现的能力。** `fill_caps` 在 D3D8 里为每个 caps 位注明实现
   它的代码；GL 这边的等价物是 `COVERAGE.md` 与 4.14 的能力表。

### 17.3 部署互斥规则不变

一个游戏目录只放一个图形 DLL。`opengl32.dll` 与 `d3d8.dll`/`d3d9.dll`/
`ddraw.dll` 不同时出现在同一目录。4.2 的 presenter 令牌是这条规则的安全网，
不是它的替代品。

## 18. 实施进度

> 本节随实现同步更新。落地一条勾一条，偏离计划的地方写清楚为什么。

### 已落地（2026-08-24）

**新增文件**（`glbridge/`，约 15.7k 行 JS）：

| 文件 | 行数 | 内容 |
| --- | ---: | --- |
| `webgpu_host.js` | 260 | adapter/device/canvas 单一所有者、presenter 令牌、设备丢失恢复 |
| `gl-webgpu/gl_constants.js` | 1132 | 217 个操作码 + 740 个 GL 枚举，从 guest 头生成；另有 149 个 registry 补充值 |
| `gl-webgpu/gl_wire.js` | 60 | 140 个声明式操作码签名，从 guest 载荷布局生成 |
| `gl-webgpu/gl_state_layout.js` | 438 | GL 状态 uniform 块，三方共用、按字段裁剪 |
| `gl-webgpu/gl_shader_translator.js` | 5117 | GLSL 1.10/1.20 完整前端 → WGSL |
| `gl-webgpu/gl_fixed_function.js` | 1109 | 固定管线签名 → WGSL |
| `gl-webgpu/gl_arb_program.js` | 830 | ARB vertex/fragment program 汇编 → WGSL |
| `gl-webgpu/gl_executor.js` | 6900 | GL 状态机、资源、绘制路径、pipeline/bind group、帧与 present、同步查询 |
| `gl-webgpu/COVERAGE.md` | 生成 | 入口点覆盖表 |
| `gl-webgpu/README.md` | — | 偏差清单（D-01..D-18）与能力声明的权威记录 |
| `tools/gen_gl_coverage.js` | 110 | 从代码生成覆盖表 |

**修改**：`v86_network_bridge.js`（GL executor 安装与路由、`glBackend` A/B 开关）、
`game.html`（脚本加载顺序）、`app.js`（`?glBackend=` 参数）。

**测试**（`glbridge/tests/`，全绿）：

| 套件 | 结果 |
| --- | --- |
| `gl_protocol_consistency_test.js` | 8 通过 —— 线格式对拍 guest 头，217 个操作码全部有 handler |
| `gl_shader_translator_test.js` | 35 通过 —— 预处理器、类型检查、重载决议、uniform 布局、varying 打包 |
| `gl_shader_wgsl_validation_test.js` | 46 对着色器过真 `naga`，其中 **Cube 2 语料 36/36** |
| `gl_fixed_function_wgsl_test.js` | 20 通过（含 naga）—— texenv 真值表、光照方程、texgen、点精灵、签名完备性 |
| `gl_arb_program_test.js` | 24 通过（含 naga）—— 写掩码、swizzle、PARAM 数组矩阵展开、相对寻址、状态绑定 |
| `gl_executor_test.js` | 31 通过 —— 状态默认值、矩阵栈、同步查询、图元展开、纹理格式展开、程序反射 |
| `v86_network_bridge_gl_route_test.js` | 通过 —— 裸 GL 流路由到 executor，D3D 信封不受影响 |

既有 D3D 套件未受影响：`d3d9_webgpu_executor_test` 156 通过、
`ddraw_webgpu_executor_test` 12 通过、d3d8/d3d9 路由测试通过。

**按里程碑对照**：

- [x] **M0** —— 覆盖表与 A/B 开关已建；调用面直方图（`opengl-call-profile.md`）
      与运行时 GLSL 语料未采集（需要在 guest 里跑）。
- [x] **M1** —— `webgpu_host.js`、上下文/共享组、后备缓冲、present（含翻转）、
      clear 走 loadOp、立即模式、图元展开、绕向反转。
- [x] **M2** —— 矩阵栈、8 光源完整方程、双面、颜色材质、雾三模式、
      texenv 全部模式含 COMBINE/DOT3/crossbar、texgen 五模式、alpha test、
      裁剪平面、全部光栅化状态、客户端数组与 VBO 直接绑定。
- [x] **M3** —— 完整格式矩阵（含通道展开规则）、1D/2D/3D/Cube、压缩纹理直传
      与 DXT CPU 解码、mipmap 生成、纹理完备性、FBO/renderbuffer/MRT、
      `glCopyTexImage*`。
- [x] **M4** —— GLSL 1.20 完整前端 + 兼容内建 + 反射 + 变体链接。
- [x] **M5** —— ARB 汇编翻译器、遮挡查询、`glReadPixels` 异步回读路径。
- [x] **M6（按 B 类偏差口径）** —— 累积缓冲、颜色
      `glDrawPixels`/`glBitmap`/`glCopyPixels`、点精灵、多边形点画、
      `glPolygonMode` 与固定/GLSL 普通 draw 的 `glLogicOp` 已实现；宽线、线点画、
      默认帧缓冲 MSAA 和深度/模板 pixel rectangle 保留为登记偏差。
- [ ] **M7** —— gl4es 与 A/B 开关已删除，host save/load 重放测试已通过；XP guest
      中的 save/load 端到端验证、render bundle 与性能对比仍待真实环境验收。

### 2026-09-01：为 guest 实机测试做的三处修复

准备用 glviewer 与魔兽争霸 OpenGL 模式实测前，审计了 guest↔host 之间两条
"不能在批次内回答"的路径，两条都是坏的——静态测试全绿正是因为它们都没被
断言过。

1. **`glReadPixels` 从来不可能成功。** host 把 status 置 PENDING，在
   `mapAsync` 回调里才写回 OK；而 guest 在 `emit_pci_batch` 返回后**只查一次**
   status，此刻回调必然还没运行（它要等浏览器事件循环）。于是每一次
   `glReadPixels` 都返回失败、目标缓冲区原样不动。修法是计划第 6.3 节一直预告
   的那条 guest 端改动：`wait_for_host_status()` 自旋等待，`Sleep(1)` 把时间片
   还给模拟器，5 秒静默上限。

2. **遮挡查询的结果从来没有被回读。** `endQuery` 往 `pendingQueries` 里推，
   但没有任何地方消费它——`resolveQuerySet` 在整个 executor 里一次都没出现，
   `query.ready` 永远是 false。后果比"结果不准"严重得多：合法的
   `while (!available) glGetQueryObjectuiv(...)` 循环会**永远转下去**。现在
   `flushFrame` 把当帧所有未决查询一次 resolve 到 buffer、copy 到 staging、
   map 后回填，并把 slot 归零复用。

   同时修掉一个连带问题：query set 是懒创建的，所以本次会话第一个
   `glBeginQuery` 遇到的 pass 是在没有 set 的情况下建的，
   `beginOcclusionQuery` 会直接触发 WebGPU 校验错误。现在 `beginQuery` 发现
   当前 pass 不带 set 就先结束它，让 `ensurePass` 重建一个带 set 的。

   guest 侧 `GL_QUERY_RESULT` 原本在结果未就绪时直接返回"可见"。现在先自旋
   1 秒争取真实答案，超时才退回保守的"可见"——宁可多画一个遮挡物，也不要漏画。

3. **`wglUseFontBitmaps` 画的是占位方块，不是字形。** 改为用
   `GetGlyphOutlineA(GGO_BITMAP)` 真正光栅化，按 GL 的 bottom-up 行序翻转、
   按 GDI 的原点约定换算 `glBitmap` 的 xorig/yorig；只在 GDI 给不出轮廓
   （点阵字体、字面缺字）时才退回方块。编译字形前用
   `glPushClientAttrib` 钉住 unpack 状态，因为那些只是*初始*值，先前上传过
   紧凑纹理的应用会把 alignment 留在 1。

`tests/gl_executor_test.js` 新增 5 条断言覆盖前两项（含一条专门断言批次返回
瞬间 status 仍是 PENDING——guest 那次单查正是死在这里）。

### 2026-09-01（二）：glview 首次在 guest 里跑，暴露的两件事

**1. 三个 backend 各自持有一个 GPUDevice —— OpenGL 的每一帧 present 都挂。**

```
[TextureView of Texture "...WebgpuSwapChainTexture..."] is associated with
[Device], and cannot be used with [Device].
 - While validating colorAttachments[0].
 - While encoding [CommandEncoder "GL frame"].BeginRenderPass(...)
```

`webgpu_host.js` 当初就是为了防这件事写的，但只有 GL executor 接了进去；
`d3d8_executor.js` 和 `d3d9_executor.js` 仍旧各自 `requestAdapter()` /
`requestDevice()` / `context.configure()`。GPUCanvasContext 属于**最后一个
configure 它的 device**，而 DDraw 走 D3D9 executor 驱动 Windows 桌面，所以在
任何 GL 程序启动之前，canvas 早就被 D3D9 的 device 占了。于是
`getCurrentTexture()` 交回来的纹理 GL 的 device 根本不能用。

这在单元测试里看不见：每个 executor 的套件都只构造它自己那一个。现在三个都从
`webgpu_host` 取 device，新增 `tests/webgpu_shared_device_test.js` 在同一块
canvas 上把三个都建起来，断言它们落在同一个 device、canvas 只被配置一次、且
配置里带着三方都需要的 usage 位。顺带修掉两个连带问题：d3d8 原本请求的 device
**不带任何可选特性**（它若先跑，共享 device 就没有 BCn）；host 的解析改成构造
时惰性求值，不再依赖 `game.html` 的 script 顺序（顺序也一并改正了，
`webgpu_host.js` 现在最先加载）。

**2. ARB 程序的错误上报是坏的。**

glview 提交了一个编译不过的 ARB 程序（一个 conformance 工具本来就会这么做），
暴露出三点：`GL_PROGRAM_ERROR_STRING_ARB` 在 host 端**根本没实现**，应用永远
拿不到失败原因；`GL_PROGRAM_ERROR_POSITION_ARB` 走 `glGetIntegerv` 这条路时落
进 `default: return 0`，也就是在程序**编译成功**时也报"第 0 个字符出错"；而
编译失败被记成 host `refusal`，让那个计数器失去了"这一帧 host 没画出什么"的
含义。现在三者都按扩展规范走 GL 自己的通道，lexer 记录字符偏移供
`ERROR_POSITION` 使用。

### 2026-09-01（三）：修复索引数据偏移

**`glDrawElements` 一直在用错误的索引画。** 打包 draw 的载荷布局是
`[固定头 16B][MT 头 12B][索引数据][客户端数组块]`，但 executor 读完固定头就
地切索引，也就是从 offset+16 切——切到的是 MT 头。实测索引 `[0,1,2]` 被读成
`[16707, 21581, 8]`，即 magic word `CAMT` 的两个半字加上 tex_unit_count。
块偏移算的是对的，所以解码不报错，画面只是错——这类 bug 最难查。

原因是测试盲区：`gl_stream_builder.js` 只有 `drawArrays`，整个套件没有一条
indexed draw 的 fixture，所以 MT 头和索引的相对位置从来没被断言过。现在补了
`drawElements` builder 和一条断言"索引必须是 guest 发的那些，不是它前面那个
头的字节"。顺带把索引的边界检查改成用声明的 `index_data_size` 而不是
`subarray` 之后的长度——后者会被静默钳位。

同时让流解码错误自带上下文：`refused op40: ... {}` 现在会带上操作码名字、
记录大小和头部的 32 位字与十六进制，对着 `opengl32_proxy.c` 里的 struct 一眼
就能看出是哪边的偏移错了。

### 未落地（按优先级）

1. **从未在 guest 里跑过。** 全部验证是静态的：JS 套件、真 `naga`（82 个着色器）、
   假 GPUDevice 驱动的 executor 测试。这是最高优先级——D3D8 迁移的教训就是
   静态验证再充分也不能替代在 XP guest 里跑一次。第一步是
   `sample/gl_triangle_test.exe`，然后是半条命，然后是 Cube 2。
2. **M0 的调用面数据未采集**：`opengl-call-profile.md` 与运行时 GLSL 语料需要
   在 guest 里跑一遍才能产出，因此排在第 1 项之后。
3. **精确语义偏差**：宽线/线点画、默认帧缓冲 MSAA、深度/模板 pixel
   rectangle，以及少数 ARB assembly 组合，详见 README 的 D-01..D-18。
4. **性能项未做**：立即模式批量记录（6.4）、连续批次合并（4.7）、render bundle
   （13.1 第 5 项）都还是原始形态——每个 `glBegin`…`glEnd` 一次 draw。
5. **save/load 的 guest 验收**：bridge 已保存并重放原始 GL 记录流，host 测试覆盖
   reset、顺序和恢复期间的新批次排队；尚未在 XP guest 的实际游戏会话中验收。

### 与计划的偏离

- **同步查询没有改动 guest 协议。** 第 6.3 节预计 `glReadPixels` 与遮挡查询
  需要新的响应区记录格式。实测不需要：批次字节是 guest 内存的活视图，host
  在 `mapAsync` 完成后直接写回原记录的 status 字，guest 只需自旋。响应区常量
  仍按 D9WG 布局定义并被一致性测试覆盖，但当前实现走的是"写回原记录"这条
  更简单的路，且 bridge 额外提供 `writeGuestMemory` 作为第二条路径。
  **guest 侧仍需改动的只有一处**：`glReadPixels` 与查询结果目前假定同步完成，
  必须改成自旋等待 status 字。
- **执行必须是同步的。** 计划没有明说这一点，但它是整条路径的硬约束：guest
  阻塞在 `DeviceIoControl` 里，端口写返回时就要读到答案。`executeBatch` 因此
  全程同步，只有设备尚未就绪时才排队。
- **`glLogicOp` 当前是拒绝而不是 10.4 描述的"拷贝附件 + 着色器整数运算"。**
  实现代价与收益不成比例（只有橡皮筋选框一个常见用法），排到 M6。
- **偏差编号扩到 D-18**（计划里到 D-14），新增的四条都是 M6 未完成项与 ARB
  混用限制。
