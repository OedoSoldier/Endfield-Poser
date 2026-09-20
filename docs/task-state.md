# 任务存档：Endfield Poser 摆姿管线（2026-08-28）

> 目的：阶段性存档，方便后续（或换会话）直接接续开发。

## 一、当前已完成的

### 游戏侧（已推送 a28d143）

- **冻结**：关 Animator + 抑制 FinalIK/布料写者 + 每帧维持；新增"冻结飘带/裙子/头发"开关（`g_freezeAccessories`，默认关 = 从骨保持实时演算），游戏内 UI / WebUI / API 三处可切。
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
