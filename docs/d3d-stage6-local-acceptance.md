# 阶段6：本地Wine与3DMark完整验收

2026-11-05用户确认的范围；主计划位于相邻v86仓库的`docs/d3d-webgpu-review.md`第8节。

阶段4、5已完成。阶段6继续完整Wine/API验收、遗留问题处理和下列六版3DMark全项验收。新增GitHub CI、每晚调度及发布自动化移出本阶段；XP与游戏磁盘镜像仅留本地，不上传GitHub。冒险岛继续跳过。

| 顺序 | 程序 | 本地镜像 | 当前状态 |
| --- | --- | --- | --- |
| 1 | 3DMark99 | `game/3dmarklegacy.img` | Metal全部26项通过；旧分支性能对齐/跨后端核对仍开放 |
| 2 | 3DMark2000 | `game/3dmarklegacy.img` | Metal全部22项通过；跨后端核对仍开放 |
| 3 | 3DMark2001 SE | `game/3dmarklegacy.img` | 全项验收待完成 |
| 4 | 3DMark03 | `game/3dmark03.img` | 全项验收待完成 |
| 5 | 3DMark05 | `game/3dmark05.img` | 全项验收待完成 |
| 6 | 3DMark06 | `game/3dmark06.img` | 全项验收待完成 |

用户已确认最后一项为06。运行前核实镜像内实际版本和完整子测试清单，记录设置、浏览器/GPU后端、DLL及宿主版本。通过本地站点实际运行，每次只开一个XP虚拟机。

通过标准：

- 每一版的全部子测试完成，包括各版提供的游戏、CPU和功能测试；默认计分子集、演示、局部场景或旧记录回放不能替代全项运行。受能力、授权或其他因素限制而跳过的项目记为未完成。
- 逐场景检查画面，保存关键帧及必要的视频证据；纹理、几何、光照、阴影、透明、特效及场景切换正确。参考画面须独立核对，完成分数不能代替画面验收；Metal/SwiftShader差异需定位。
- 从启动到结果页保留完整浏览器日志，console错误、页面未捕获异常、Promise拒绝及WebGPU错误均为零；不能隐藏错误后宣称通过。设备丢失、超时、资源清理异常须处理，告警保留供复核。
- 本地保留可重复运行的脚本、逐项结果与证据。修复发现的缺陷并补回归，每完成一个功能批次在v86与本站的`refactor`分支分别提交相关改动。

既有3DMark2001 SE局部场景回放仅证明其覆盖范围。完整Wine device审计仍有10次skip及上游漏Release问题；其他Wine套件与本表六版验收完成前，阶段6保持开放。

## 本地复现与证据导出

在相邻 `v86` 仓库执行 `node tools/d3d_guest_review.mjs` 构建当前三个代理 DLL、启动脚本和只读工具光盘。运行器默认启用 CPU Worker，与站点正常配置一致；旧的主线程诊断成绩不能与之混算。

```sh
D3D_GAME_OUT=build/d3d-review/stage6-99-local \
D3D_GAME_WORKER=1 D3D_GAME_MANUAL=1 D3D_GAME_BOOT_MS=80000 \
D3D_GAME_LAUNCHER=mark99 D3D_GAME_TIMEOUT_MS=2400000 \
node tests/glbridge/d3d_game_browser_runner.mjs 3dmarklegacy
```

依次使用 `mark99`、`mark00`、`mark01` 配合 `3dmarklegacy`；后三版使用 `mark03`/`3dmark03`、`mark05`/`3dmark05`、`mark06`/`3dmark06`。手动选择全部项目并保留设置截图。运行器从页面导航前收集页面和 Worker 错误，每五秒保存客体画面及图形统计。`control.json` 提供本机调试地址；`inputs.json`、`environment.json`、`core-assets.json` 和 `host-manifest.json` 标记输入与宿主版本。运行期间不要替换其工具光盘或宿主文件。

99 的结果页保存项目时保留“Save Image Quality test pictures”选项，再在 Result Browser 中选中项目并导出文本报告。等 XP 完成文件写回后执行：

```sh
node tests/glbridge/d3d_benchmark_artifacts.mjs \
  build/d3d-review/stage6-99-local RESULT.TXT PROJECT.3DP \
  '3DMark 99 Max/ddraw.dll' 'Quality/*'
```

此工具通过公开磁盘导出接口读取 RAM 覆盖层，支持 CPU Worker；提取文件并保存 SHA-256 后删除本次临时分块，不修改原始镜像。缺失文件会失败并保留分块，不能当作通过。客体尚未写回时需稍后重试。验收完成记录必须包括全部项目、画面复核、错误统计与正常退出；`captureD9WG()` 的局部场景捕获不能代替完整基准验收。

### 阶段6首批修复与99结果（2026-11-06）

修复逐绘制重复扫描ClipStatus造成的客体CPU开销、索引UP上传整个未使用顶点区间的问题，以及宿主压缩队列缺少限流导致的十几GB原始记录积压。缓存逐字比较全部位置（包括未引用顶点），可变位置/视口/非法索引仍被正确处理；屏幕内部顶点使用保守快速路径，边界及异常值保留原计算。队列限流保留全部历史，不丢弃绘图或隐藏错误。

3DMark99 Max build 200在Metal完成全部26项：1605分，赛车22.7 FPS、第一人称12.4 FPS；同为CPU Worker的修复前为189分、6.2/1.1 FPS。18张质量图片已导出；9张完整图扣除元数据后与修复前像素完全一致，另核对了随程序提供的32位软件参考图与场景关键帧。浏览器/宿主/GPU错误、失败命令和丢弃绘制均为零；正常退出后设备/资源为零。客体程序目录内DLL SHA-256与发布DLL一致。

**99的完整Metal运行已通过，旧分支性能对齐仍开放。** 旧分支仅观察到赛车瞬时约42 FPS，随后发生历史队列内存失败，不能把它当作完整平均成绩。此批不声称达到旧分支全部性能，也不声称完成SwiftShader全项比较、其余五版或完整Wine/API验收。详细版本、哈希、设置及证据见 `d3d-stage6-1-validation.json`；大体积图片/报告仅在本地 `v86/build/d3d-review/stage6-99-scalar` 保存。

### 3DMark2000完整Metal验收（2026-11-06）

Pro build 340在1024×768、16位颜色/纹理/深度、三缓冲、硬件T&L与CPU Worker下完成全部22项，得分2799、CPU分161。直升机低/中/高为71.1/47.0/15.0 FPS，Adventure为51.0/29.6/19.6 FPS；其余填充率、1/4/8光源、8/16/32/64MB纹理、四种凹凸映射均有实际成绩。两类画质测试输出18张图片，9张完整图与关键场景已检查，并核对自带帮助中的独立图例和高光差异说明。

浏览器console错误、宿主/GPU错误、失败命令和丢弃绘制均为零，正常退出后设备/资源为零。保留两类告警：测试切换时客体窗口暂时隐藏；坐标诊断样本为未绑定纹理的纯颜色绘制。没有隐藏告警或把它们改成通过条件。原始详情表由`BENCHUI --list Details`读取，项目、DLL、成绩表和18张图片通过RAM磁盘导出核验。详见 `d3d-stage6-2-validation.json`。SwiftShader比较、其余四版及完整Wine/API验收继续开放。

测试已发布DLL时可设置`D3D_REVIEW_USE_SHIPPING=1`构建工具光盘，确保客体加载字节与站点DLL完全一致。2001启动脚本现在先打开设置界面，必须选择所有测试后开始，避免默认计分子集被误记为全项。
