# 任务存档：Endfield Poser 摆姿管线（2026-08-28）

> 目的：阶段性存档，方便后续（或换会话）直接接续开发。

## 〇、2026-09-21 发布状态（v0.2.0）

**本轮新增（游戏内体验为主）**

- 骨骼参数面板：3D 点选骨骼后可调 **旋转/位置**（滑条 + 数值输入 + 复位；humanoid 复位到 A-pose，从骨复位到冻结瞬间）
- **撤销/重做**（Ctrl+Z / Ctrl+Y，整骨架快照，连续拖拽按 400ms 合并成一步）
- **输入路由重做**：按命中区域 + 光标状态决定鼠标归属。`plugin/poser_config.txt` 里 `click_through=1` 时覆盖层常驻显示并真穿透（`WS_EX_LAYERED|TRANSPARENT`），按住 Alt 或游戏自己放开光标时才接管；文本输入临时抢焦点
- 从骨链：默认不显示；勾「从骨链」显示可摆放链根；命名过滤 `Nub/Twist/corrective/Collider/表情骨/inner/outer/wep`
- 关节缓存 128 → 1024（之前溢出导致"有些关节点不动"）

**本轮修复（第二轮收口）**

- 姿态保存补全：文件新增 `acc`（从骨绝对 local 变换）与 `morphs`（形态键权重），旧文件依旧可读；**相机参数仍不进文件**
- 裙摆识别修复：原来只匹配 `MC_Skirt`，实际命名是 `MC_Chen_Skirt` 这类 → 改成不区分大小写匹配 "skirt"，并会打印每个 BeyondBoneCloth 的 GO 名便于核对
- 禁用插件 / GUI 线程退出时自动收尾：解冻 + 恢复物理与形态键（避免头发布料停留在冻结姿态）
- `click_through` 默认改为 1（覆盖层常驻 + 真穿透）

**已知问题（仍未修）**

- **IK 控制器未生效**：`src/editor/ik_control.h` 已实现（目标点 + 平移手柄 + 解析式 2-bone 解算），实测拖拽后骨骼不跟随，用 `g_ikFeatureEnabled = false` 整体关闭（面板/解算/绘制都跳过）
- **直启必崩**：必须经启动器启动（原因与判据见 `AGENT.md` §3）
- 撤销不覆盖形态键权重（形态键改动不进撤销栈）
- 相机参数不随姿态文件保存

**雪藏**：Blender 桥（`tools/blender/`、`docs/blender-bridge.md`，见下文 §二）

**已移除（做过但没做好的）**：骨骼层级树面板（Blender 风格树 + 搜索）。实测不理想，发布前已删，
代码保留在 git 历史里（提交 `05a1b51`）。

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
- git push 需走代理：`git -c http.proxy=http://127.0.0.1:7897 push`（Clash Verge 混合端口 7897）。
