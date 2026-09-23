# 任务存档：Endfield Poser 摆姿管线（2026-08-28）

> 目的：阶段性存档，方便后续（或换会话）直接接续开发。

## 〇、2026-09-21 发布状态（v0.2.0）

**本轮新增（游戏内体验为主）**

- 骨骼参数面板：3D 点选骨骼后可调 **旋转/位置**（滑条 + 数值输入 + 复位；humanoid 复位到 A-pose，从骨复位到冻结瞬间）
- **输入路由重做**：按命中区域 + 光标状态决定鼠标归属。`plugin/poser_config.txt` 里 `click_through=1` 时覆盖层常驻显示并真穿透（`WS_EX_LAYERED|TRANSPARENT`），按住 Alt 或游戏自己放开光标时才接管；文本输入临时抢焦点
- 骨骼显示：默认只有 humanoid（干净）；勾「全量骨骼(微调)」显示全部可摆放骨（含从骨链）。命名过滤 `Nub/Twist/corrective/Collider/表情骨/inner/outer/wep` 两种模式都生效
- 关节缓存 128 → 1024（之前溢出导致"有些关节点不动"）

**本轮修复（第二轮收口）**

- 姿态保存补全：文件新增 `acc`（从骨绝对 local 变换）与 `morphs`（形态键权重），旧文件依旧可读；**相机参数仍不进文件**
- 裙摆识别修复：原来只匹配 `MC_Skirt`，实际命名是 `MC_Chen_Skirt` 这类 → 改成不区分大小写匹配 "skirt"，并会打印每个 BeyondBoneCloth 的 GO 名便于核对
- 禁用插件 / GUI 线程退出时自动收尾：解冻 + 恢复物理与形态键（避免头发布料停留在冻结姿态）
- `click_through` 默认改为 1（覆盖层常驻 + 真穿透）

**已知问题（仍未修）**

- **IK 控制器未生效**：`src/editor/ik_control.h` 已实现（目标点 + 平移手柄 + 解析式 2-bone 解算），实测拖拽后骨骼不跟随，用 `g_ikFeatureEnabled = false` 整体关闭（面板/解算/绘制都跳过）
- **直启必崩**：必须经启动器启动（原因与判据见 `AGENT.md` §3）
- **表情无法跨角色通用**：面部由 SMC 骨骼变形驱动（Grid 上无 BlendShape，`BlendShapes rebuilt: 0 slots`），
  SMC 权重不写入姿态文件；跨角色套用面部骨数据会错位/夸张，故姿态文件已排除命名过滤命中的表情骨
  （`brow/eye/face/lip…`）。要做"表情随姿态共享"得先把 SMC 权重按角色签名序列化
- 撤销不覆盖形态键权重（形态键改动不进撤销栈）
- 相机参数不随姿态文件保存

**雪藏**：Blender 桥（`tools/blender/`、`docs/blender-bridge.md`，见下文 §二）

**渲染 API 验证（2026-09-21）**：DX11 与 **Vulkan** 两种模式均实测可用。Vulkan 下插件由
`vulkan-1.dll` 代理拉起（日志 `[PROXY] plugins loaded via vulkan-1.dll (Vulkan path)`）；
注意必须用窗口化/无边框全屏，独占全屏会绕过 DWM 合成导致覆盖层面板不可见。

**按角色记忆冻结（2026-09-22）**：`src/game/char_state.h` 按角色（Animator 物体名去掉 `(Clone)#NN`）
记住 冻结标记 + 姿态（按骨名，用 PoseDoc；不含表情骨）。切走保存、切回恢复、没冻过的角色保持默认不冻结；
`freeze.h` 的 `FrozenGrip` + `MaintainFrozenGrips()` 让后台已冻结角色持续保持压制，
解冻/禁用插件时 `ReleaseGripFor` / `ReleaseAllGrips` 归还写者。限制：只有当前角色可编辑。

**已移除（做过但没做好的）**：

- 骨骼层级树面板（Blender 风格树 + 搜索）——实测不理想（提交 `05a1b51`）
- 撤销/重做 + 操作序列 + 清空姿态 + 回 A-pose——实测问题多（撤销栈与冻结维持/形态键纠缠、
  清空姿态会破坏姿态、A-pose 基线来自动画帧导致朝向偏），已在第二轮收口时整体移除
- 「从骨链」复选框——含义不直观（默认只显示链根的中间粒度），已移除；从骨改由「全量骨骼」访问

代码都在 git 历史里，需要时再挑回来重做。

## 一、当前已完成的

### 游戏侧（已推送 a28d143）

- **冻结**：关 Animator + 抑制 FinalIK/布料写者 + 每帧维持；"冻结飘带/裙子/头发"开关（`g_freezeAccessories`，**默认开** = 连从骨一起冻住；取消勾选则从骨保持实时演算），游戏内 UI / WebUI / API 三处可切。
- **姿态库 HTTP 化**：`/api/poses`、`/api/poses/save|load|delete`，WebUI 侧栏可输入名称保存/载入/删除（绕开游戏输入拦截）。
- **删除自研 IK 与游戏内摆姿 UI**：`ik_driver.h`（回弹根源）、`panel_pose.h`（FK 面板+火柴人覆盖层）、`gizmo.h`、`panel_camera.h`、`panel_mode.h`；选中状态迁到 `editor/selection.h`。
- 保留：骨骼采集/姿态回写 API、形态键、SMC 表情、布料/配件抑制、WebUI、外部控制通道。

### ARP 学习（docs/arp-ikfk-study.md + tests/mike_anatomy.py + tests/mike_pose_probe.py）

- 结论：FK/IK 双链 + `ik_fk_switch` 驱动蒙皮骨两组约束影响度；切换必须 Snap。
- 关键配方：`rotFK.influence = var`、`rotIK.influence = 1-var`（var 指向 `c_hand_ik.l["ik_fk_switch"]`）；`add_driver_to_prop` 见 ARP `src/lib/drivers.py`。

## 二、已雪藏：Blender 侧控制 Rig（暂时搁置）

> **状态：已雪藏（2026-09-20）**。桥接插件源码（`tools/blender/endfield_poser_bridge/`）
> 与方案文档（`docs/blender-bridge.md`）都保留，但暂不继续开发、也不作为当前对外功能；
> 游戏侧不依赖 Blender 也能完成摆姿（旋转盘 + WebUI）。恢复时从本节步骤接着做。

目标：桥接插件（`tools/blender/endfield_poser_bridge/__init__.py`）在游戏骨架上生成 ARP 风格双链。

1. **识别四肢链**：游戏端 `/api/bones` 增加 HumanBodyBones 枚举值字段（`h`），Blender 端按枚举定位 shoulder→upperarm→lowerarm→hand / thigh→calf→foot，避免按名字猜。
2. **手臂先行**：生成 `c_arm_fk/arm_fk`、`c_forearm_fk/forearm_fk`、`c_hand_fk` FK 链 + `arm_ik/forearm_ik` IK 链 + `c_hand_ik` 目标 + `c_arms_pole` pole；游戏原始骨变蒙皮骨，挂 `rotFK/rotIK` 成对 COPY_ROTATION + driver。
3. **写回**：`sync_to_game` 只导出蒙皮骨（游戏骨骼）最终姿态，控制骨不写回；现有 delta 换算逻辑保留。
4. **验证**：无头测试仿照 `tests/mike_pose_probe.py`：切 IK/FK 看蒙皮骨是否跟随正确；再复制到腿 + snap 操作符 + 面板按钮。

## 二·五、游戏侧补一个"简易旋转盘"（用户新需求，未开始）

用户要求游戏内也保留一个简单摆姿入口：

- 冻结后角色上**直接渲染骨骼**（叠加层，git 历史 `a28d143^` 的 `DrawWorldBonesOverlay` 可恢复）；
- **点击骨骼选中**（屏幕空间最近关节/线段拾取，简单版即可）；
- 选中骨上出现 **UE 风格旋转盘**（ImGuizmo ROTATE 或自绘圆环；`deps/imguizmo` 仍在，但 `ImGuizmo.cpp` 需加回 CMake 源）；
- 拖动旋转盘 = FK 旋转该骨（localRotation 写回），仅冻结态可用；
- 与 Blender 控制 Rig 并存：这只是快捷 FK 入口，数据仍走 `/api/pose`。

## 三、环境备忘

- Blender 锁定 **5.2 LTS**（插件已实测；ARP manifest 无上限版本）。
- 桥接插件源码在 `tools/blender/endfield_poser_bridge/`，安装位置 `%APPDATA%\Blender Foundation\Blender\5.2\scripts\addons\endfield_poser_bridge\`。
- 无头测试跑法：
  `& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup -P <script>`（需提权）。
- git push 走本机 HTTP 代理：`git -c http.proxy=http://127.0.0.1:10090 -c https.proxy=http://127.0.0.1:10090 push`
  （端口取自系统代理设置 `HKCU:\...\Internet Settings\ProxyServer`；换机器时先 `Test-NetConnection 127.0.0.1 -Port <port>` 确认）。

## 四、未来计划：共享姿态库（未开始）

目标：玩家摆好的姿态可以上传到公共库，其他人按角色筛选、预览、一键下载。

**前置技术点**

1. **跨角色适配**：姿态按骨名存，不同角色骨架不同 → 每份姿态要标注**适用角色**（模型名，如
   `chr_0005_chen_postmodel`），应用时报告骨匹配率（如 55/55 骨匹配）。
2. **表情不参与**：SMC 权重不进姿态文件（跨角色会错乱），共享姿态 = 身体 + 从骨；库里要写明这一点。
3. **只传数据**：JSON 纯数据；客户端要校验字段范围（位置/旋转限幅）、文件大小上限、拒绝路径穿越。

**路线（每步可独立上线）**

- **阶段 0（建议先做，成本最低）**
  - 姿态文件加元数据：`format`（格式版本）、`char`（适用角色）、`author`、`tags`、`created`；
  - WebUI 加"**从 URL 导入**"：粘贴直链（GitHub raw 等）→ 下载到 `plugin\poses\`，
    不需要任何服务器就能和群里的人互换姿态。
- **阶段 1（零运维）**：把 GitHub 仓库当库——`library/` 目录收姿态文件，投稿走 PR / issue 附件，
  CI 自动生成 `index.json`（列表 + 角色 + 作者 + 标签 + 下载地址）；插件/WebUI 读索引浏览与一键下载。
  优点：免费、有历史、合并即审核；缺点：投稿门槛对有 GitHub 账号的人友好，对纯玩家略高。
- **阶段 2（自建服务）**：薄 REST 服务（列表 / 详情 / 上传 + 缩略图）+ 对象存储 + 后台审核队列；
  到这里才需要域名、服务器与内容审核责任。
- **阶段 3（社区功能）**：账号、点赞收藏、标签/角色筛选、热度排序。

**缩略图**：优先用 3D 骨骼叠加层离屏渲染一张"火柴人姿势预览"（本地生成、风格统一、无隐私问题）；
备选是上传时附游戏内截图（插件内截图目前未实现，见"已知问题"）。

**风险与合规**：只托管姿态 JSON 与预览图，**不要托管模型/贴图等游戏资源**；加文件大小限制、
内容去重（hash）、举报与下架入口。

**下一步（未开工）**：先做阶段 0 的元数据 + URL 导入，观察群里互传的使用情况（角色分布、
平均骨匹配率），再决定走 GitHub 库还是自建服务。

## 五、未来计划：摄影模式里直接点选角色编辑（未开始）

目标：摄影模式里场上常有多个角色，希望能**点哪个角色就编辑哪个**，而不是只能改"当前主角色"。

### 现状分析（哪些已经就绪）

- **按角色记忆冻结**（`src/game/char_state.h` + `freeze.h` 的 `FrozenGrip`）已经实现：
  角色身份（模型名去掉 `(Clone)#NN`）、冻结标记、姿态快照、后台角色持续压制写者——**这些都是"切换编辑目标"必需的底座**。
- 换角色的既有管线也是现成的：置 `g_charAnimator` + `g_charChanged` → `GameFrameTick` 会重建
  humanoid / 从骨 / 形态键 / SMC，再由 `RestoreCharStateOnSwitch()` 恢复该角色状态。
- 3D 拾取也有基础：`rig_gizmo.h` 已把关节投影到屏幕并缓存（`g_jointSx/Sy/Trans`），点击即最近关节选中。

### 需要做的三件事

1. **枚举场上角色**：`Object.FindObjectsOfType(Animator)`（需在 `game_hooks.h` 里解析该方法）
   或走游戏自己的角色管理器；筛掉非 humanoid（`Animator.get_isHuman`）与已销毁对象。
   只在"面板打开 / 点刷新"时枚举一次，不要每帧跑（`FindObjectsOfType` 不便宜）。
2. **切换编辑目标**：把选中的 Animator 设为 `g_charAnimator`（同时刷新 `g_charAnimComp` 与相关偏移），
   置 `g_charChanged`，其余交给既有重建 + 状态恢复管线。风险点是**非玩家角色**的
   `ComplexAnimationComponent` 字段布局可能不同——现有代码的字段扫描回退（`Animator found via field scan`）
   正好覆盖这种情况，但要实测。
3. **交互入口**（两个，可分两步做）：
   - 先做**面板列表**：主面板加一个"角色"下拉/列表（显示模型名 + 是否冻结），点一下即切换。**成本最低、立刻可用**；
   - 再做**3D 点选**：为所有候选角色缓存一份轻量关节表（只需 transform + 名字，用于投影与命中），
     点击时先判断落在哪个角色的关节附近 → 切换目标并选中那根骨。注意与现有"点空白取消选中"的语义协调。

### 风险与注意

- 非玩家角色的动画可能由 AI/行为树驱动，压制写者的组件类名集合可能与主角色不同（现有采集是按类名 +
  递归 transform，理论上通用，但需要实测补充类名）。
- 角色被卸载后其 Transform 指针失效：只做"按名匹配 + SEH 兜底"，不要长期持有裸指针
  （`char_state.h` 已是这种模式）。
- 面板/列表要显示"当前编辑目标"，否则用户会以为是主角色。

### 工作量与建议顺序

- 面板列表切换：约半天（枚举 + UI + 切换调用），风险低；
- 3D 点选：再加半天到一天（多角色关节缓存 + 命中测试 + 与选中语义协调）；
- 合计约 1.5～2 天，建议**紧跟"共享姿态库阶段 0"之后**做——两者都动到"角色身份"，
  姿态库的阶段 0 也需要在文件里记录 `char` 字段，正好共用同一套角色标识。
