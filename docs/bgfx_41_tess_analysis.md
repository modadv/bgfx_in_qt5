# bgfx 示例 `41-tess` 实现原理分析

## 1. 示例目标与核心思路

`41-tess` 的目标不是使用固定功能硬件 Tessellation（HS/DS），而是用 **Compute Shader + 间接调度 + 实例化绘制** 在 GPU 上实现自适应地形细分（Adaptive Tessellation）。

它参考的是论文/实现链路中的 **Implicit Subdivision** 思想（`tess.cpp` 文件头注释已给出来源），核心策略是：

1. 只保留非常粗的基础网格（这里是 2 个大三角形，覆盖 `[-1,1]x[-1,1]`）。
2. 用一个 `key` 编码二叉细分树中的某个子三角形。
3. 每帧 compute 根据相机距离计算目标 LOD，并对每个 `key` 做「细分/保持/合并」。
4. 结果写入可见列表，再用 `drawIndexedIndirect` 直接驱动实例化绘制，CPU 不回读、不重建 mesh。

这是一套“**GPU 完整驱动细分与绘制计数**”的方案。

---

## 2. 代码组成与职责拆分

### 2.1 C++ 侧（调度与资源管理）

- `external/bgfx.cmake/bgfx/examples/41-tess/tess.cpp`
  - 初始化 bgfx、相机、UI。
  - 创建程序（draw/compute）、贴图、各种 buffer。
  - 每帧按固定顺序 dispatch compute + submit draw。

### 2.2 Shader 侧（算法本体）

- Compute
  - `cs_terrain_init.sc`: 初始化间接参数、初始 key、计数器。
  - `cs_terrain_update_indirect.sc`: 根据上一帧写入量更新 dispatch 参数。
  - `cs_terrain_lod.sc`: 主算法，做 LOD 细分决策和视锥裁剪。
  - `cs_terrain_update_draw.sc`: 根据可见 key 数量更新 drawIndirect 参数。
- Draw
  - `vs_terrain_render.sc`: 按实例 ID 取 `(primID,key)`，重建子三角形，采样高度图位移。
  - `fs_terrain_render.sc`: 基于坡度图计算简单明暗。
  - `fs_terrain_render_normal.sc`: 输出法线可视化。
- 公共头
  - `terrain_common.sh`: 位移采样、LOD 计算、key 更新规则、atomic 写入。
  - `isubd.sh`: key 与局部细分变换（`key -> transform`）的数学实现。
  - `fcull.sh`: AABB vs Frustum 裁剪。
  - `uniforms.sh`: 参数打包与线程规模宏。

---

## 3. 几何与数据表示

## 3.1 基础地形网格（Coarse Mesh）

`tess.cpp::loadGeometryBuffers` 定义了 4 个顶点 + 2 个三角形：

- 顶点：`(-1,-1), (1,-1), (1,1), (-1,1)`
- 索引：`{0,1,3, 2,3,1}`（两片大三角）

也就是说“真实地形细分”并不来自 CPU 网格，而是来自后续 key 驱动的隐式重建。

## 3.2 key 编码的细分树

`isubd.sh` 把每个子三角形编码为一个 `uint key`：

- 根 key 为 `1`（`isRootKey`）。
- 子节点由位扩展得到（`childrenKeys`）。
- `findMSB_(key)` 近似得到层级深度（LOD 级）。
- `keyToXform` 把 key 解析成从根三角形到目标子三角形的仿射变换。

这样一来，不需要存储所有细分顶点，只要存储 `(primID, key)` 就能在 VS/CS 中重建该子三角形的三个顶点。

## 3.3 核心缓冲区

`tess.cpp` 中主要使用动态 index buffer 作为“uint 数组容器”（开启 compute read/write）：

- `m_bufferSubd[2]`：ping-pong 的输入/输出细分列表（每项两个 uint：`primID,key`）。
- `m_bufferCulledSubd`：可见子三角列表（也是 `primID,key` 对）。
- `m_bufferCounter`：3 个原子计数器，供 compute 阶段统计写入量。
- `m_dispatchIndirect`：2 组间接参数
  - 一组给 `dispatchIndirect`（compute）
  - 一组给 `drawIndexedIndirect`（draw）

---

## 4. 初始化流程（一次性或重启时）

关键入口在 `tess.cpp::init` 与 `update` 的 `m_restart` 分支：

1. `loadPrograms()`  
   加载 2 个 draw program + 4 个 compute program。
2. `loadTextures()`  
   - `loadDmapTexture`: 读 `R16` 高度图。
   - `loadSmapTexture`: 在 CPU 上由高度图梯度计算 `RG32F` 坡度图。
3. `loadBuffers()`  
   建立 coarse mesh、细分列表 buffer、实例化 patch 网格。
4. `createAtomicCounters()`  
   建立 3 元素原子计数器 buffer。
5. `createIndirectBuffer(2)`  
   创建 2 条 indirect 命令槽位。
6. 首帧（或切换 patch 级别后）运行 `cs_terrain_init`：
   - 初始化 draw/dispatch indirect 参数。
   - 写入两个根三角形 `(0,1)` 与 `(1,1)` 到输入/输出/cull 列表。
   - 重置计数器（例如当前输入条目数=2）。

---

## 5. 每帧渲染管线（最重要）

以下顺序来自 `tess.cpp::update`，是示例的核心时序：

1. 计算并提交 uniform（`u_DmapFactor/u_LodFactor/u_cull/u_freeze/u_gpu_subd`）。
2. 若非重启：先运行 `cs_terrain_update_indirect`  
   - 从上一帧计数器读出“当前待处理 key 数”，更新下一次 `dispatchIndirect` 的线程组数量。
3. 运行 `cs_terrain_lod`（通过 indirect dispatch）  
   - 对每个输入 key：
     - 重建子三角形顶点。
     - 计算当前/父级目标 LOD。
     - 执行细分规则（细分/保留/合并）。
     - 执行视锥裁剪，写入可见列表。
4. 运行 `cs_terrain_update_draw`  
   - 按可见列表计数更新 `drawIndexedIndirect` 的 `numInstances`。
5. Draw：`submit(..., m_dispatchIndirect)`  
   - VS 根据 `gl_InstanceID` 读取第 N 个 `(primID,key)`，重建该实例对应的子三角 patch。
   - 对 patch 内顶点做位移（采样 dmap）。
   - FS 使用 slope map 估计法线，做灰度光照或法线可视化。
6. `m_pingPong = 1 - m_pingPong`  
   - 下一帧交换输入/输出细分列表。

这是典型的 GPU 驱动图：

`SubdIn -> (CS LOD) -> SubdOut + Culled -> (CS update draw) -> IndirectDraw -> VS/FS`

---

## 6. 关键算法拆解

## 6.1 LOD 目标计算

`terrain_common.sh` 的 `computeLod`：

1. 估计点到相机距离 `z`（在 view 空间测距）。
2. 用 `distanceToLod(z, lodFactor)` 计算需要细分的层级。
3. `lodFactor` 来自 C++：
   - 与视场角 `fovy`、屏幕宽度、patch 级别、目标“每边像素长度”相关。
   - 意义是把几何细节控制到目标屏幕像素密度。

因此这个 LOD 是 **屏幕空间误差驱动** 的近似实现。

## 6.2 细分状态转移（updateSubdBuffer）

`terrain_common.sh::updateSubdBuffer` 对每个 key 做三选一：

1. `subdivide`：若当前层级低于目标且可继续细分且可见，则写入两个子 key。
2. `keep`：若当前层级仍满足父级约束且可见，则保留自己。
3. `merge`：否则尝试并回父级（通过特定位约束只让 half sibling 执行，避免重复写父节点）。

配合原子追加写入（`atomicFetchAndAdd`）实现并行安全。

## 6.3 视锥裁剪

`fcull.sh` 先从 `MVP` 提取 6 个平面，再做 AABB-平面测试：

- AABB 来自该子三角形三个点的 min/max。
- z 范围额外扩展到 `[0, u_DmapFactor]`，把位移高度考虑进包围盒。

裁剪通过后才写入 `u_CulledSubdBuffer`，直接影响最终实例数量。

## 6.4 间接调度闭环

本示例非常关键的一点是“**compute 写 indirect，下一阶段直接消费 indirect**”：

- `cs_terrain_update_indirect` 写下一次 `dispatchIndirect` 参数。
- `cs_terrain_update_draw` 写本帧 `drawIndexedIndirect` 参数（尤其 `numInstances`）。

所以 CPU 不需要知道“本帧有多少可见 patch”，也不需要 map/readback 计数器。

---

## 7. Draw 阶段如何重建真实三角形

`vs_terrain_render.sc` 的重建逻辑：

1. `threadID = gl_InstanceID` 定位到第 N 个可见 key。
2. 从 `u_CulledSubdBuffer` 拿到 `(primID,key)`。
3. 从 coarse mesh 取该 `primID` 的三个顶点 `v_in[3]`。
4. `subd(key, v_in, v)` 计算目标子三角形三个顶点。
5. 使用 instanced patch 网格中的局部重心坐标 `a_texcoord0` 做 `berp` 插值得到当前顶点位置。
6. 采样 `dmap` 修改 `z`，得到最终地形表面。

这里的“instanced patch 网格”来自 `tess.cpp` 中 `s_verticesL0..L3 / s_indexesL0..L3`：

- `gpuSubd = 0/1/2/3` 对应 patch 内预细分精度 `1/4/16/64` 三角形。
- 这相当于“每个 key 实例内部再用固定网格细分一次”，提升曲面平滑度。

---

## 8. 参数与开关对行为的影响

UI 控件在 `tess.cpp::update` 中绑定到 uniform：

- `Pixels per edge` -> `u_LodFactor` 的目标像素误差。
- `Triangle Patch level` -> `u_gpu_subd`，影响 patch 内固定细分密度，变更时触发 `m_restart` 重建。
- `Cull` -> `u_cull`，是否启用视锥裁剪。
- `Freeze subdividing` -> `u_freeze`，冻结 LOD 演化（用于观察/调试）。
- `Shading` 切换两个 FS（灰度光照 vs 法线可视化）。

---

## 9. 这个示例“为什么高效”

1. CPU 只做参数与资源管理，不参与细分拓扑更新。
2. 每帧 patch 数由 GPU 自己统计并驱动间接 draw。
3. 子三角形顶点通过 key 解析即时重建，不存储大规模细分顶点缓冲。
4. 视锥裁剪在 compute 前置，减少 draw 实例数。
5. ping-pong 输入/输出列表，避免读写冲突。

---

## 10. 与你当前工程对照时可重点借鉴的点

如果你的高度图渲染已经参考该示例，建议重点对照以下“不可缺少”机制：

1. `key` 编码与 `keyToXform/subd` 是否保持一致。
2. `counter + indirect` 闭环是否完整（dispatch/draw 参数是否完全由 GPU 更新）。
3. merge 规则是否避免父节点重复写入。
4. culling 的 AABB 是否考虑位移高度范围。
5. patch 内固定细分级别变化时是否重建 instanced patch mesh。

---

## 11. 关键源码定位（便于快速跳转）

- 主循环与调度：`external/bgfx.cmake/bgfx/examples/41-tess/tess.cpp`
- LOD compute：`external/bgfx.cmake/bgfx/examples/41-tess/cs_terrain_lod.sc`
- 间接参数初始化：`external/bgfx.cmake/bgfx/examples/41-tess/cs_terrain_init.sc`
- 间接 dispatch 更新：`external/bgfx.cmake/bgfx/examples/41-tess/cs_terrain_update_indirect.sc`
- 间接 draw 更新：`external/bgfx.cmake/bgfx/examples/41-tess/cs_terrain_update_draw.sc`
- key 与细分数学：`external/bgfx.cmake/bgfx/examples/41-tess/isubd.sh`
- 公共细分逻辑：`external/bgfx.cmake/bgfx/examples/41-tess/terrain_common.sh`
- 裁剪逻辑：`external/bgfx.cmake/bgfx/examples/41-tess/fcull.sh`
- 顶点重建：`external/bgfx.cmake/bgfx/examples/41-tess/vs_terrain_render.sc`
- 片元着色：`external/bgfx.cmake/bgfx/examples/41-tess/fs_terrain_render.sc`

---

## 12. 一句话总结

`41-tess` 的本质是：**用 key 编码的隐式细分树，在 compute 中做并行 LOD+裁剪，并通过 GPU 维护的 indirect 参数直接驱动实例化绘制，从而实现 CPU 低参与度的自适应地形渲染。**

