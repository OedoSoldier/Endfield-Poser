# Blender 桥接方案（Endfield Poser ↔ Blender）

> 目标：用 Blender 作为摆姿界面（其 Pose 模式成熟直观，自带 3D 手柄、姿态镜像、
> 约束等），把游戏角色骨骼桥接进 Blender，在 Blender 里摆姿势，实时同步回游戏。

## 为什么走这条路

- 游戏内 HUD/手柄问题太多（ACE 拦输入、相机矩阵、窗口焦点），且"掰不动"。
- Blender 本身就是专业摆姿/绑骨工具，等于免费获得理想的"人身上控制器"。
- 桥接走 localhost HTTP（已实现服务器），不碰窗口消息/输入注入，ACE 管不着。

## 架构

```
[游戏进程] poser.dll（注入）
   │  HTTP JSON 服务器 127.0.0.1:18923
   ▼
[Blender] Python 插件 EndfieldPoserBridge
   │  - 连接游戏，拉取骨骼/姿势
   │  - 生成 Armature（骨骼层级与世界位姿对齐）
   │  - Pose 模式编辑 → 发回游戏
   ▼
[游戏] 冻结态下应用姿势（骨骼 localRotation/localPosition 写回）
```

## 坐标系换算（关键难点）

Unity/终末地：**Y 轴向上**、左手系。Blender：**Z 轴向上**、右手系。

换算：绕 X 轴旋转 -90°（Unity Y-up → Blender Z-up）：
```
B_pos = (U_x, U_z, -U_y)     # 位置
B_rot = R_x(-90°) * U_rot     # 旋转（四元数）
```
骨骼局部旋转同理：Blender pose bone 的 rotation_quaternion 需要先算
「游戏当前姿势 − 游戏基准姿势」的 delta，再映射到 Blender 的 rest 系。

**推荐 v1 策略（避免 rest pose 难题）**：
1. 连接时把游戏**当前姿势**捕获为 Blender 的 rest pose（Armature 直接按当前位姿摆）。
2. Blender 中编辑的是相对 rest 的 pose 变换。
3. 同步回游戏时：目标 localRotation = restLocalRot ⊗ poseDelta。
   即 Blender pose delta（rest→current）换算成游戏空间，右乘到游戏的 rest localRot。

## 同步方向与防回环

- 单向开关：`游戏→Blender`（导入当前姿势）或 `Blender→游戏`（摆姿同步回）。
- 避免两个方向同时轮询造成回环。
- 建议交互：Blender 侧加一个 "Sync to Game" 按钮/快捷键（或拖动时实时发），
  冻结状态下应用。

## 插件 API（已就绪）

| 端点 | 说明 |
|------|------|
| `GET /api/bones` | 骨骼列表：名、父、世界位姿、本地旋转(四元数)/位置 |
| `GET /api/pose` | 全部骨骼当前 localRotation/localPosition（JSON 数组） |
| `POST /api/pose` | 应用整姿（数组，跳过锁定骨） |
| `POST /api/setbonerot` | 设置单骨绝对 localRotation（四元数） |
| `POST /api/setbonepos` | 设置单骨绝对 localPosition |
| `POST /api/freeze` | 冻结/解冻（on: true/false） |
| `POST /api/tpose` / `reset` | T-pose / 复位到冻结快照 |
| `POST /api/drag` | 面板拖拽 FK（web UI 用，桥接可不用） |

## Blender 插件实现步骤

1. **连接与建臂**：`bpy` 创建 Armature，按 `/api/bones` 的 parent 链 + 世界坐标
   建 Edit Bones；把当前姿势设 rest。
2. **导入姿势**：读 `/api/pose`，将每个骨骼的 localRotation 映射到 pose bone。
3. **导出姿势**：遍历 pose bones，把 Blender delta 换算回游戏空间，`POST /api/pose`。
4. **面板 UI**：N 面板（sidebar）放 Connect / Sync→Game / Sync←Game / 自动同步开关。
5. **自动同步**：可选，Blender 的 `frame_change_pre`/定时器轮询。

## 里程碑

- [ ] M1：Blender 插件骨架——连接、建臂、rest 对齐（验证坐标换算）
- [ ] M2：单向 游戏→Blender 姿势导入（在 Blender 里看到游戏当前姿势）
- [ ] M3：单向 Blender→游戏 导出（在 Blender 摆姿势，游戏角色跟着动，冻结态）
- [ ] M4：体验打磨——自动同步开关、骨骼命名过滤、镜像、关键帧可选

## 现状备注

- 已部署 build（04:52）含 web 线程 IL2CPP 附加修复（不再 abort）+ 桥接 API。
- Web UI（http://127.0.0.1:18923）仍可用作兜底/快速查看。
- 游戏内 HUD 已不必要（隐藏时冻结维持照常跑）。
