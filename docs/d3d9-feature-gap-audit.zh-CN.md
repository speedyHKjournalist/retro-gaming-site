# D3D9 → WebGPU：距离 9.0a/b/c 全特性还差什么

审计日期：2026-08-23。基线：3DMark 2006 全部测试可跑完。

> **修订 2026-08-23 14:10** —— 初稿把 bump environment mapping 列为 P0 第一大缺口，
> 这是错的：该特性在工作区（未提交）里已经实现，审计期间正好有并行工作在改同一批
> 文件。详见 P0 第 1 条。其余条目已按当前工作区重新核对。

## 审计方法

对照的不是 MS Learn 的散文页，而是三份机器可核对的清单——它们才是"9.0a/b/c
全特性"的实际定义：

1. `IDirect3D9` / `IDirect3DDevice9` / 各资源接口的 vtable（`d3d9.h`）
2. `d3d9types.h` 里每个枚举的**全域取值**（`D3DRENDERSTATETYPE`、
   `D3DTEXTUREOP`、`D3DSAMPLERSTATETYPE`、`D3DTEXTURESTAGESTATETYPE`、
   `D3DQUERYTYPE`、`D3DTEXTUREADDRESS`、`D3DFILLMODE`…）
3. `D3DCAPS9` 的每个字段与每个 bit

每一项都追到了实现点或缺口点，行号见条目。

## 总体结论

**API 面是完整的。** `IDirect3D9` 的 17 个方法、`IDirect3DDevice9` 的 119 个方法、
所有资源接口都已挂上 vtable，没有空洞。

缺口全部在**枚举值的覆盖率**和**状态的消费率**上：guest 侧把状态收全了并送到了
host，host 侧只消费了其中一部分。

| 枚举 | 已消费 / 全域 |
| --- | --- |
| `D3DRENDERSTATETYPE` | 68 / ~103 |
| `D3DTEXTUREOP` | 21 / 26 |
| `D3DSAMPLERSTATETYPE` | 9 / 13 |
| `D3DTEXTURESTAGESTATETYPE` | 18 / 18 |
| `D3DQUERYTYPE` | 5 / 14 |
| shader 指令 | 拒绝 4 条（`UNSUPPORTED_OPS`，`d3d9_shader_pipeline.js:130`）|

---

## P0：真实游戏会踩、且画面会错 —— 已全部完成（2026-08-23）

实现要点与偏差：

- **五个 texture op**：`MODULATEALPHA_ADDCOLOR` / `MODULATECOLOR_ADDALPHA` /
  `MODULATEINVALPHA_ADDCOLOR` / `MODULATEINVCOLOR_ADDALPHA` 四个已按 D3D9 文档的
  代数式实现，并只在 colour 通道有效（D3D9 本身也只为 `D3DTSS_COLOROP` 定义它们，
  alpha 通道仍然拒绝并计数）。`PREMODULATE` 仍然拒绝：它要与**下一段**的纹理相乘，
  而 cascade 没有任何向后传值的通道。
- **`MaxStreams` 16**：guest 侧 16 个槽位。host 不为此绑 16 个 WebGPU vertex
  buffer —— layout 只按*声明实际引用*的 stream 建，超过 8 个（WebGPU 保证的
  `maxVertexBuffers`）才拒绝并计数。
- **LOD**：`D3DSAMP_MAXMIPLEVEL` 与 `SetLOD`（新增协议 opcode `0x222`）都映射到
  `lodMinClamp`，取两者中更严格的一个；`D3DSAMP_MIPMAPLODBIAS` 因为 WebGPU sampler
  根本没有 bias 字段，改为在采样点用 `textureSampleBias` 实现，FFP 与翻译后的
  pixel shader 两条路都覆盖。bias 以字面量烘进 WGSL 并量化到 1/16 级，避免逐帧
  改 bias 的标题每帧新建管线。
- **Gamma ramp**：新增协议 opcode `0x223`。在 present 那一步（每个像素上画布前的
  最后一站，也正是硬件应用它的位置）做 256 项查表。identity ramp 会被识别并退回
  普通 copy，不付全屏 pass 的代价。**偏差**：真实 D3D9 只在全屏设备上生效，这里
  窗口模式也生效。
- **`D3DRS_FILLMODE`**：WebGPU 没有 polygon mode，wireframe 改为把每个三角形重写成
  三条边的 line-list（strip 会先展开）。这是精确实现而非近似。`D3DFILL_POINT` 退化为
  1 像素点 —— WebGPU 在 point-sprite 路径外没有点尺寸，这是唯一的偏差。
- **`MIRRORONCE`**：sampler 本来就已经选了 clamp-to-edge，补上坐标的 `abs()` 就是
  精确实现，不是近似。FFP 与翻译路径都覆盖。
- **`SPHEREMAP`**：用 `GL_SPHERE_MAP` / wined3d 的同一套公式（标题的球面贴图美术
  就是照这个做的）。
- **flat shading**：`@interpolate(flat)`（WGSL 默认 provoking vertex 是 first，与
  D3D9 一致），只作用于两个颜色 varying；纹理坐标在 D3D9 里 flat 模式下仍然插值。
  翻译后的 pixel shader 不受影响 —— D3D9 不会按 shade mode 重新插值 shader 的输出。

新增 caps：`D3DPRASTERCAPS_MIPMAPLODBIAS`、`D3DPTADDRESSCAPS_MIRRORONCE`、
`D3DVTXPCAPS_TEXGEN_SPHEREMAP`、四个 `D3DTEXOPCAPS_MODULATE*_ADD*`。
协议版本 1.4 → 1.5。测试：executor 145 项、shader pipeline 42 项、
naga 校验 178 个着色器，全绿；DLL 以 `-Werror` 编译通过。


- [x] 1. Bump environment mapping 全家桶 —— 已由并行工作完成，只剩 4 条冷门指令
- [x] 2. 五个 texture op 缺失 —— 四个已实现，PREMODULATE 仍拒绝
- [x] 3. `MaxStreams` 4 → 16
- [x] 4. Mip LOD 控制整条链（`MIPMAPLODBIAS` / `MAXMIPLEVEL` / `SetLOD`）
- [x] 5. `SetGammaRamp` 落地
- [x] 6. `D3DRS_FILLMODE`（wireframe / point）
- [x] 7. `D3DTADDRESS_MIRRORONCE`
- [x] 8. `D3DTSS_TCI_SPHEREMAP`
- [x] 9. `D3DRS_SHADEMODE = D3DSHADE_FLAT`

### 1. Bump environment mapping 全家桶 —— 已完成

**2026-08-23 更新：这一项在本次审计写完之前就已经由并行进行的工作实现了，
审计初稿把它列为最大缺口是错的。** 当前工作区（未提交）里已经有：

- `D3DTOP_BUMPENVMAP`(22) / `BUMPENVMAPLUMINANCE`(23) 在 cascade 里
  （`d3d9_executor.js:1859`），含两段耦合逻辑（`isBumpSource`，7537 起）
- `D3DTSS_BUMPENVMAT00..11`、`BUMPENVLSCALE`、`BUMPENVLOFFSET` 全部消费并上传
  （`d3d9_executor.js:9826` 与 `9916`）
- ps_1_x 的 `texbem` / `texbeml` / `texdp3` / `texdp3tex` / `texm3x2pad` /
  `texm3x2tex` / `texm3x3pad` / `texm3x3tex` / `texm3x3spec` / `texm3x3vspec`
  已翻译（`d3d9_shader_pipeline.js` 的 `emit()`）

`D3DTEXTURESTAGESTATETYPE` 因此从 12/18 变成 18/18。

- `bem`（ps_1_4 的算术形 bump，无纹理采样）也已翻译：2026-08-28 补上，
  3DMark 2001 的 Advanced Pixel Shader 测试整个建立在它之上，之前 guest 侧
  `CreatePixelShader` 直接拒绝该 shader，测试跑不起来

剩余仍被拒绝的只有 3 条冷门指令（`UNSUPPORTED_OPS`，`d3d9_shader_pipeline.js`）：

- [ ] `texm3x2depth` / `texdepth`（用纹理寻址结果改写 fragment depth）
- [ ] `texm3x3`（Radeon 世代写 3x3 结果的变体）

优先级低：这三条在实际游戏里近乎绝迹。

### 2. 五个 texture op 缺失

`PREMODULATE`(17)、`MODULATEALPHA_ADDCOLOR`(18)、`MODULATECOLOR_ADDALPHA`(19)、
`MODULATEINVALPHA_ADDCOLOR`(20)、`MODULATEINVCOLOR_ADDALPHA`(21)。（初稿写"七个"是把已实现的两个 bump op 误算在内。）

2002-2004 年的 FFP 引擎里非常常见（镜面高光叠加、贴花）。

### 3. `MaxStreams = 4`，D3D9 是 16

`d3d9_proxy.c:697` 的 `D9_MAX_STREAMS`。骨骼动画 + 顶点色 + 多套 UV 拆流的引擎
会直接超。

### 4. Mip LOD 控制整条链缺失

`D3DSAMP_MIPMAPLODBIAS`(8)、`D3DSAMP_MAXMIPLEVEL`(9)、
`IDirect3DBaseTexture9::SetLOD`。

caps 里已经诚实地关掉了 `D3DPRASTERCAPS_MIPMAPLODBIAS`，但游戏的"纹理质量"
设置项会静默失效。

### 5. `SetGammaRamp` 是彻底的空函数

`d3d9_proxy.c:5512` —— 函数体只有一串 `(void)` 转型。而 `fill_caps` 在
`d3d9_proxy.c:3196` 以 `/* device_set_gamma_ramp(). */` 为注释宣告了
`D3DCAPS2_FULLSCREENGAMMA`。这是一条已经失效的 caps 承诺：游戏的亮度滑条不起作用。

修法很短：把 ramp 带到 host，在 present 的 blit 里做 LUT。

### 6. `D3DRS_FILLMODE`

wireframe / point 填充完全没实现。WebGPU 没有 polygon mode，必须把三角形索引
改写成 line-list。

### 7. `D3DTADDRESS_MIRRORONCE` 静默退化成 clamp

注意：executor 已经为 `BORDER` 做了 shader 端的坐标域外替换，`MIRRORONCE` 完全
可以走同一条路。这不是"WebGPU 无法表达"，只是没做。

### 8. `D3DTSS_TCI_SPHEREMAP`

被计数拒绝，`d3d9_executor.js:7465`。

### 9. `D3DRS_SHADEMODE = D3DSHADE_FLAT`

未消费。WGSL 有 `@interpolate(flat)`，几乎零成本。

---

## P1：规范内、影响面较窄 —— 已全部完成（2026-08-23）


- [x] **`ProcessVertices` 已实现**（guest 侧软件顶点管线，从 D3D8 路径移植）。
      必须在 guest 侧做：这个调用的全部意义就是结果落在 app 能 Lock 读取的内存里，
      而本栈的 host 是异步的，没有任何同步 GPU 往返能回答它。覆盖 world/view/
      projection 变换、透视除法、viewport 映射、方向光/点光/聚光的环境与漫反射项、
      以及 diffuse/specular/texcoord/psize 的透传，支持 `D3DPV_DONOTCOPYDATA`。
      **两处具名限制**（不是静默降级）：绑定了 vertex shader 时拒绝（用固定管线顶替
      会悄悄产出不同的几何）；`D3DRS_SPECULARENABLE` 的高光不计算，destination 要
      SPECULAR 就透传 source 的 —— 与 D3D8 路径的做法一致。
      配套新增自检冒烟测试 `sample/d3d9_process_vertices_test.c`：它不看像素，
      Lock destination 直接**核对数字**，所以乘法次序错、少了透视除法、viewport
      偏移差半个像素都会失败而不是变成"看起来还行"的画面。
- [x] **`SetSoftwareVertexProcessing` 与 `D3DCREATE_` 行为标志**：`BehaviorFlags`
      此前被完全忽略。现在 `CreateDevice` 校验它（必须恰好命名一种顶点处理模式；
      `PUREDEVICE` 只能配 `HARDWARE`），混合设备上 `SetSoftwareVertexProcessing`
      真正生效并能读回，非混合设备只接受它已有的模式。
- [x] **`D3DCREATE_PUREDEVICE`** 已实现：D3D9 文档列出的 27 个 Get\* 全部返回
      `D3DERR_INVALIDCALL`。这个代理其实*保留*着那份影子状态（Reset 和 state block
      需要它），所以回答这些调用很容易 —— 但那样是错的：app 要了 pure device 却拿到
      答案，等于被告知"你的性能提示被采纳了"，而它花代价想避免的跟踪其实还在跑。
- [ ] **`CreateDevice` 的 `BehaviorFlags` 被完全忽略**：代码里 `D3DCREATE_` 一次
      都没出现。`D3DCREATE_PUREDEVICE` 要求所有 `Get*` 失败；
      `SOFTWARE_VERTEXPROCESSING` 要求无视 caps 接受任意版本 shader 和 256 个常量。
      两条语义都没实现。
- [x] **`Surface::GetDC` / `ReleaseDC` 已实现**，纯 guest 侧：DIB section 是唯一
      一种 GDI 既肯往里画、又肯交出裸指针的位图，所以 DC 存在期间 surface 的像素
      同时活在两处，`ReleaseDC` 时拷回并走普通 LockRect 的上传路径。wined3d 也是
      这么做的，原因相同。只允许 D3D9 规定的四种未压缩显示格式；DC 存在期间 surface
      保持 LockRect 锁定（这正是 D3D9 的规定）；surface 析构时回收 GDI 对象，
      不指望调用方。
- [x] **`CreateAdditionalSwapChain` 已实现**，贯穿三层（协议 1.6，新增
      `0x224/0x225/0x226`）：
      - **关键设计**：附加链的后台缓冲是**普通的 render-target 纹理**，而不是第二个
        "handle 0" 特例。这样 SetRenderTarget、StretchRect、GetRenderTargetData 和
        整个 render pass 构建器全都不用改 —— 它们本来就会把纹理当目标。链只在
        "把成品搬上它自己的 canvas" 这一步才特殊。
      - executor 通过 `createSwapChainCanvas` 钩子向宿主要一块画布（只有页面知道
        第二块 overlay 该放在文档的哪里），bridge 提供默认实现并按屏幕缩放定位。
      - present 走 **blit 而非 copy**：后台缓冲的格式是 guest 要什么就是什么，而
        canvas 是首选 canvas 格式，`copyTextureToTexture` 要求两者一致而它们通常不一致。
      - 宿主没提供钩子时，链的帧会被**计数并报告**，不是静默渲染到虚空。
      - 两处具名拒绝：多重采样的附加链（需要 present 时 resolve，没实现），以及
        附加链上的 `GetFrontBufferData`（转发给设备会读到*另一个窗口*的像素，
        错误的数据比拒绝更难归因）。
- [x] **`D3DRS_WRAP0..15`**：确认**无法实现** —— wrap 是逐三角形比较三个顶点的
      坐标后做的决定，WebGPU 没有任何能看到整个图元的阶段（几何着色器或逐 draw 的
      compute 预处理是仅有的两种可行形态）。现在会显式报告并说明症状（柱面/球面
      UV 的接缝），不再静默忽略。
- [x] **`D3DRS_MULTISAMPLEMASK`** 已映射到 `multisample.mask`。
      `D3DRS_MULTISAMPLEANTIALIAS=FALSE` 近似为只写 sample 0。
      **顺带修掉一个真实 bug**：渲染管线此前从不声明 `multisample`，而附件是
      多重采样的 —— WebGPU 要求两者一致，所以任何 MSAA 设备的每一次 draw 都会
      校验失败。现在 sample count 从 render target 一路带到管线并进了缓存键。
- [x] **Query 5/14 不是缺口** —— 复核后撤回。缺的九种全是可选的驱动性能计数器
      （`VCACHE`、`RESOURCEMANAGER`、`VERTEXSTATS`、各类 `*TIMINGS`），真实零售
      驱动同样以 `D3DERR_NOTAVAILABLE` 拒绝它们，而代理已经返回的正是这个错误码。
      伪造计数器只会让 app 相信一些编造的数字。真正重要的五种（`EVENT`、
      `OCCLUSION`、三种 `TIMESTAMP`）都已实现。
- [x] **`D3DCAPS2_CANAUTOGENMIPMAP` 已打开**：`d3d9_proxy.c:3199` 的
      注释说 AUTOGENMIPMAP "deliberately refused"，但 2D 和 cube 两条路径现在都
      实现了（executor 5626 / 6031，proxy 6145 / 9574）。注释和代码已经不同步，
      caps 少报了一个已经能用的特性。
- [x] **StretchRect 不是缺口** —— 复核后撤回。格式转换**已经支持**（走 blit
      管线，正是它覆盖缩放与格式转换）。只有压缩目标被拒，而 D3D9 本身也拒绝：
      StretchRect 的目标必须是 render target 或 offscreen plain surface，
      BCn 格式无法成为 render attachment。

---

## P2：规范要求但游戏几乎不用（高阶图元）—— 已完成（2026-08-23）

**关键判断：WebGPU 没有细分阶段，所以全部在 guest 侧求值，结果作为普通的
indexed triangle list 画出去 —— 协议和 host 一行都不用改。** 等东西过线的时候，
它已经只是几何了。

在 CPU 上做不是"绕过缺失的阶段"，而是唯一输入齐全的地方：控制点就在这个 DLL 已经
影子化的顶点缓冲里，细分级别是逐调用的参数而不是管线状态。



- [x] **`DrawRectPatch` / `DrawTriPatch`** —— 张量积 / 三角 Bézier 曲面在 guest
      侧求值。限制全部**具名拒绝**而非近似：只支持 `D3DBASIS_BEZIER`（B-spline 和
      Catmull-Rom 的控制点语义不同，拿 Bézier 顶替会画出一个光滑但位置错误的曲面）；
      次数到 cubic 为止（这正是 `D3DDEVCAPS_RTPATCHES` 不带 `QUINTICRTPATCHES` 的
      承诺）；每次调用一个 patch；顶点分量支持 FLOAT1..4 与 D3DCOLOR（打包色会先
      解包成四个浮点再插值再打包 —— 把 D3DCOLOR 当浮点插值是把 patch 顶点色变成
      噪声的经典做法）。
      caps 新增 `D3DDEVCAPS_RTPATCHES | D3DDEVCAPS_RTPATCHHANDLEZERO`。
- [x] **`DeletePatch`** 保持返回 `D3DERR_INVALIDCALL` —— 这里不缓存任何 patch，
      而 caps 报的是 `RTPATCHHANDLEZERO` 而非缓存形式，所以读 caps 的 app 根本不会
      走到这里来失望。
- [x] **N-patch（PN triangles）已实现**：`SetNPatchMode > 1` 之后，每个三角形
      draw 的每个三角形都会被替换成由三个顶点的位置和法线构造的三次 Bézier 三角形
      （Vlachos 等人的构造）。**要点**：它坐在绘制热路径上，所以整件事以
      `npatch_segments` 非零为闸门 —— 默认情况下每次 draw 只多一次浮点比较。
      `D3DRS_POSITIONDEGREE` 与 `D3DRS_NORMALDEGREE` 都被遵守，且默认值正确
      （三次位置 + **线性**法线 —— 悄悄把法线升成二次会改变 app 没要求改的光照）。
      无法细分的 draw（没有法线、绑了 shader、输出顶点超出 16 位索引）会**回落到
      平面绘制**而不是被丢弃：缺法线的网格应该照常出现，只是不圆。
      caps 新增 `D3DDEVCAPS_NPATCHES` 与 `MaxNpatchTessellationLevel = 64`；
      `D3DUSAGE_NPATCHES` 从"未实现"列表里移除。
- [x] **`SetNPatchMode` 恢复接受**（此前我把它改成了诚实拒绝，现在它有了真实现）。
- [ ] **自适应细分 + 位移贴图**：确认**无法实现**，现在会具名报告。
      它不是"可以近似的 N-patch 级别细化"：D3D9 把它绑死在位移贴图上 —— 逐边的
      细分级别来自在**细分过程中**采样 `D3DDMAPSAMPLER` 的纹理，而没有细分阶段的
      管线里没有任何地方能在那个时刻采样纹理。开启
      `D3DRS_ENABLEADAPTIVETESSELLATION` 现在会说明原因，而不是让 app 疑惑自己的
      网格是不是坏了。`D3DUSAGE_DMAP` 仍在未实现列表里。
- [x] **`D3DFMT_MULTI2_ARGB8` 不是缺口** —— 复核后撤回。它不在支持格式表里，
      `CheckDeviceFormat` 和所有 Create* 都会拒绝它，而这正是**不支持它的真实硬件
      的行为**（只有少数 GeForce 部件支持过）。`D3DSAMP_ELEMENTINDEX` 只在多元素
      纹理上有意义，`D3DSAMP_DMAPOFFSET` 只在位移贴图上有意义 —— 两者都随各自的
      特性一起不适用。

配套新增冒烟测试 `sample/d3d9_patch_test.c`：三条路径各画一个视口，自动核对
caps 与调用返回值、`GetNPatchMode` 的回读、以及 B-spline 基**被拒绝**而不是被
当成 Bézier 画出来。几何本身仍需肉眼看：两张 Bézier 控制网是共面且均匀的，
正确求值应当重现它们描述的平面四边形和三角形。

---

## P3：规范外，但 9.0c 时代游戏真的依赖 —— 已完成（2026-08-23）

这些 vendor hack 之所以长这样，是因为 D3D9 从来没为它们长出过正式的状态或格式：
厂商把 FOURCC 塞进本来另有含义的 render state，或者塞进 `D3DFORMAT`。

**四个能精确映射，两个撞上真实的墙。** 后两个是**具名拒绝**而不是静默忽略 ——
理由见各条。

- [x] `INTZ` / `DF16` / `DF24`（深度当纹理读）—— 早前已实现。
- [x] **`ATI1N` / `ATI2N`（3Dc）** —— 这两个就是 BC4 / BC5 的 DX10 之前的名字，
      WebGPU 在 `texture-compression-bc` 下原样提供，所以这是**改名而非翻译**。
      法线图只存 X/Y 由 shader 重建 Z，比 DXT5 好到 2005-2007 的标题会直接出这种
      美术资源。要点是块大小：BC4 每 4x4 块 8 字节、BC5 是 16，弄错步长会毁掉每一级。
- [x] **`NULL`（空 render target）** —— 让 depth-only pass 不必分配一块不会被读的
      颜色缓冲。尺寸仍要匹配，逐纹素代价不必，所以映射到 `r8unorm`。
- [x] **`ATOC`（alpha-to-coverage）** —— WebGPU 原生有
      `multisample.alphaToCoverageEnabled`，是这一族里少见的精确映射。只在多重采样
      目标上生效（单采样时没有 coverage 可分摊，WebGPU 也会直接拒绝这个组合）。
- [ ] **`RAWZ`** —— 确认**无法忠实实现**。它交给 shader 的不是深度值，而是深度缓冲的
      **字节**摊在 A/R/G 三个通道里，由 app 用一个硬编码的点积重新拼装。用真实深度
      纹理去服务它，app 会拿到干净的深度然后把它解码成垃圾 —— 这比拒绝更糟，因为
      画面会是错的而不是缺的。所有探测 RAWZ 的标题都会先探测 `INTZ`，而后者已实现。
- [ ] **`RESZ`（深度 resolve）** —— 确认**无法实现**，而且是硬墙不是缺功能：host 的
      深度附件是 `depth24plus-stencil8`，**WebGPU 根本不给这个格式任何 copy-source
      能力**，所以没有任何操作（copy / blit / resolve）能把它读出来；WebGPU 也没有
      多重采样深度 resolve。而这个 trick 想绕开的需求 —— 深度缓冲同时作为附件和纹理 ——
      **已经由 INTZ 直接采样满足了**，标题在这里应该走那条路。
- [ ] **`NVDB`（深度边界测试）** —— WebGPU 在任何层级都没有深度边界状态，也没有东西
      能近似它。命名它尤其重要，因为它是**宽松地**失败：开了深度边界却被忽略的 app
      会画出它本想剔除的像素，看起来像深度 bug 而不是缺失的特性。

---

## 建议顺序

1. **Bump mapping 那一组**是唯一"大"的工作，而且 D3D8 和 D3D9 两条路径共用收益，
   排第一。
2. **七个 texture op + `MaxStreams` 16 + LOD bias** —— 都是小改动、高覆盖。
3. **gamma ramp、`MIRRORONCE`、flat shading** —— 各自半天。

## 横向问题：caps 与实现已经漂移

- `FULLSCREENGAMMA` 报了但没做
- `CANAUTOGENMIPMAP` 做了但没报
- `SetNPatchMode` / `SetSoftwareVertexProcessing` 假装成功

`d3d9proxy/README.md` 里写了 "verify a cap is backed by real code before
advertising it" 这条纪律，但没有自动化守卫——上面三条就是在没有守卫的情况下漂出来的。

- [ ] 加一个一致性测试，把 `fill_caps` 的每个 bit 对到 executor 里的实现点，
      比逐条人工复查更能防住下一次漂移。

## 已确认没有缺口的部分

避免重复审计，以下都追到了实现：cube / volume 纹理、8 段 texture stage、
MRT（4 个）、6 个 user clip plane、point sprite、vertex blending（含 indexed，
256 个 world matrix）、palette（协议 1.4）、MSAA（back buffer + render target）、
硬件 shadow map（comparison sampler）、vs_3_0 顶点纹理采样、`vPos` / `vFace`、
two-sided stencil、separate alpha blend、instancing（`SetStreamSourceFreq`）、
`TRIANGLEFAN` 索引展开、sRGB 读写、depth bias / slope-scaled、
`IDirect3D9` 全部枚举与 Check* 方法、readback / occlusion query。
