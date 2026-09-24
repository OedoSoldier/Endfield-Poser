# 任务存档：Endfield Poser 摆姿管线（2026-08-28）

> 目的：阶段性存档，方便后续（或换会话）直接接续开发。

## 〇、2026-09-24 v0.3.3：XXMI 兼容与输入修复（**已发布**）

- **热键"有时有用有时没用"**：根因是 `GetAsyncKeyState(vk) & 1` 的按下锁存位会被
  游戏/XXMI/3DMigoto 抢走。改成独立线程 5ms 轮询 + 自己判断上升沿（`HotkeyPollThread`）。
- **F12 让游戏卡住**：装了 XXMI 走的分层窗口路径原来每帧整屏 GPU→CPU 回读（4K ≈33MB/帧）。
  改成按 ImGui 顶点包围盒算脏矩形（并与上一帧矩形求并集），只回读/上传那一块；
  另加每秒一行的 `[GUI] layered present ... copy/map/ulw` 耗时日志。
- **崩溃隐患**：分层模式没有 swap chain，`WM_SIZE` 里却无条件 `ResizeBuffers` → 改分辨率/全屏切换崩。
  现在分层模式走 `LayeredSyncSize()`。
- 迟注入的 XXMI 会在 DComp 重试期间复检；检测到第三方 d3d11.dll 且热键是裸 F10~F12 时面板提示。
- **面板内改键**：主面板 `快捷键（可改）` → 改键 → 按新键 → 写回 `poser_config.txt`；
  支持 Ctrl/Shift 前缀与单键；插件自己输入框打字时热键暂停；热键只在前台是游戏时响应。
- **默认键改成 `L` / `P`**：XXMI/3DMigoto 直接轮询 F11/F12，连 `Ctrl+F12` 也会触发它们的动作。
- 踩坑记录：`build\obj\poser.res` 被删除后，**第一次**跑 build 脚本的 rc 步骤会失败
  （`rc reported success but ... is missing`），再跑一次或手动 rc 即可；发版前务必确认
  `FileVersion` 已是新版本号。
- 附注：Applepie 管理器的热键接口只有 VK 码（`AP_GetHotkeys` 返回 `currentVK`），
  看不到 `CTRL+` 修饰键——在管理器里改键会写回裸 VK，覆盖掉面板里设的组合。

## 一、2026-09-23 本轮主题：表情（SMC）修复 —— **v0.3.2 已发布**

> 代码在 `src/game/smc_morph.h`（主体）、`src/editor/panel_morph.h`、`src/poser.cpp`。
> 版本号已提到 **0.3.2**，发布包 `EndfieldPoser-v0.3.2.zip`。
> 踩过的坑：`build\obj\poser.res` 是残留旧资源时，DLL 会一直报旧版本号
> （build 脚本会先删它，但文件被占用时删除会静默失败）——发版前务必确认
> `(Get-Item plugin\poser.dll).VersionInfo.FileVersion` 是新版本。

**根因 1：多角色同场时锁错了 SMC 实例**
摄影模式里每个角色都有自己的 `SkeletalMorphCore`，且都会调用
`DoEvaluateMorphToBoneJob`。原实现是"谁先跑谁被锁"（`HookedSMCMorphJob` 里
`s_confirmedSMC = param1`），于是滑条算出来的面部增量写到了**别的角色**脸上，
当前编辑的角色"怎么拖都没反应"。日志证据：同一角色两次切换拿到不同 rig
（ardelia#45 先 5280/89 后 5996/99），18 次切换里 14 次锁到同一指针。

修复：**归属匹配**。用两重判据确认实例属于当前编辑角色——①面部骨指针与
当前角色骨骼列表（`s_allBones`）有交集；②面部骨挂在当前角色根 Transform 下。
不匹配就把它加进拒绝名单、归还大列表、重锁下一个候选；连续否掉 6 个或长时间
没有匹配则退回旧的"先到先得"并打日志（不会把能用的角色一起弄坏）。
实测日志：`accepted instance ... bone overlap 87/99`、`reject instance ... no bone overlap 0/99`。

**根因 2：表情默认值取的是"锁定那一刻游戏的脸"**
原来 `s_faceRestPose` 是锁定时读到的位姿，那张脸本身可能正在笑 → 滑条变成
"在表情上叠表情"，而且每次锁定基准都不一样。

修复：**中性脸基线**（对应身体的 A-pose 基准）。实例锁定后让 job hook 把 morph
大列表增量**整表清零 3 帧**，游戏把骨骼写回"零 morph"位姿，那时抓到的才记为默认值。
代价：锁定时脸会有约 3 帧（50ms）中性闪动。

**冻结 = 保持当前表情（不再把脸拉回中性）**
冻结瞬间用上一帧（未冻结、游戏驱动）的脸做**反解**：以捕获到的 morph 增量为基向量，
坐标下降 3 轮拟合出各滑条权重（滑条值始终表示"相对中性默认值的权重"），拟合不出
的残差折进驱动基准 `s_driveBase`（`base = P − Σ w₀×增量`）。
于是 `w = w₀` 时脸**精确等于冻结前那张脸**，不跳变；以后拖滑条就是在默认值体系下加减。

- 「全部归零」= 权重 + 基准一起复位 → 回到中性默认脸
- 「读入当前表情」= 先复位基准再填游戏侧权重，与滑条同一坐标系
- 顺带修掉：驱动时"清零游戏 morph 增量"原来没有 gate 在冻结态，解冻后脸会一直不动
- 顺带修掉：`Resolved %d/%d` 的分母算错（写死 2×19=38，实际 33）

**诊断痕迹**（下轮可直接接着用）：`[SMC] weight source probe start` 会打印
`SkeletalMorphCore` / `MainEmotion` / `EmotionPose` 三层的字段名 + 声明类型，以及
`m_allMorphs` / `m_phonemesWeights`(0x320) / `m_microExpressionWeights`(0x330) /
`m_mainEmotion`(0x3C0) 的两种解释（managed 对象 / 内联 native 缓冲）。

**已知限制（这版之后）**

- 滑条只覆盖 5 口型 + 19 表情（EIEM 表）。角色表情里用到的**其它 morph** 滑条表示不了，
  但会被"基准残差"保住（脸不跳），代价是「全部归零」后可能残留一点表情；
  残留大小看日志 `hold current face: ... residual=`。
- 「读入当前表情」的权重来源还没定论：优先走 `MainEmotion._pose` 的
  `(_ctrlName, _value)` 列表，取不到才退回 `m_allMorphs[morphId]`。若面板显示
  `no usable source (see log)`，把 `weight source probe` 那几行发出来即可定型。
- `m_allMorphs` 的元素结构（首次探测时抛异常 → 说明不是 managed 对象指针，
  多半是内联缓冲）待按探测日志定案。
- 旧文里"表情无法跨角色通用 / 撤销不覆盖形态键 / 相机参数不入姿态文件"等条目
  仍以各自所在章节为准；本节只覆盖表情驱动链路的修复。

**同期试过又拿掉的：半身镜像**

- 接了「镜像 L→R / R→L」两个按钮（复用 `MirrorPose`），实测**全错**：老实现把骨骼的
  *局部*旋转按 `(-x,y,z,-w)` 镜像、*局部*位置按 `(-x,y,z)` 镜像 —— 位置那条会把
  父骨系里的骨节长度翻反，骨架直接散。
- 改成"不猜局部轴"的版本（在角色根坐标系里对左右平面反射，平面法线由左右肩位置推出，
  只镜像旋转、不动位置）后仍有大量 bug，用户判定不值得继续 —— **整块功能已移除**
  （UI 与 `MirrorPose`/`SymPair`/镜像辅助函数都删了，代码在 git 历史里）。
- 结论：这游戏的左右骨局部轴约定不明确，要做镜像得先有一份可靠的"左右骨对应关系 +
  轴向实测"（例如逐骨对比静息四元数），否则就是反复试错。

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
   - **先做面板列表（推荐的第一步）**：主面板加一个「角色」列表——把场上角色逐个列出来，
     每行显示"角色名（模型名）+ 状态（未冻结 / 已冻结 / 正在编辑）"，**点谁就编辑谁**；
     旁边配一个"刷新"按钮重新枚举。因为冻结状态与姿态本来就是按角色存的，
     **每个角色天然各自独立**：切走的保持冻结不动，切回的恢复离开时的姿势，没冻过的保持默认。
     列表里还可以给每个已冻结角色一个"解除冻结"小按钮（按角色 key 找到它的 `FrozenGrip` 释放即可，
     不必先切过去）。
   - 再做**3D 点选**：为所有候选角色缓存一份轻量关节表（只需 transform + 名字，用于投影与命中），
     点击时先判断落在哪个角色的关节附近 → 切换目标并选中那根骨。注意与现有"点空白取消选中"的语义协调。

### 风险与注意

- 非玩家角色的动画可能由 AI/行为树驱动，压制写者的组件类名集合可能与主角色不同（现有采集是按类名 +
  递归 transform，理论上通用，但需要实测补充类名）。
- 角色被卸载后其 Transform 指针失效：只做"按名匹配 + SEH 兜底"，不要长期持有裸指针
  （`char_state.h` 已是这种模式）。
- 面板/列表要显示"当前编辑目标"，否则用户会以为是主角色。
- **同名角色会撞 key**：现在的角色标识只取模型名去掉 `(Clone)#NN`，场上若出现两个同模型角色
  （双胞胎/同一个角色的两份），他们的冻结状态会互相覆盖 → 需要把 key 细化为
  "模型名 + 出现序号"（或模型名 + entity 指针），并保证刷新时序号稳定。
- 角色显示名：模型名（`chr_0005_chen_postmodel`）对玩家不友好，先按模型名显示、
  后续可从实体上读角色显示名做映射。

### 工作量与建议顺序

- 面板列表切换：约半天（枚举 + 列表 UI + 切换调用 + 同名 key 细化），风险低；
- 3D 点选：再加半天到一天（多角色关节缓存 + 命中测试 + 与选中语义协调）；
- 合计约 1.5～2 天，建议**紧跟"共享姿态库阶段 0"之后**做——两者都动到"角色身份"，
  姿态库的阶段 0 也需要在文件里记录 `char` 字段，正好共用同一套角色标识。
