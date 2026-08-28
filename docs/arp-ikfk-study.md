# ARP（Auto Rig Pro）IK/FK 架构学习笔记

> 来源：Auto Rig Pro 官方行为文档 + 官方免费 Mike 演示绑定（CC-0，Blender 4.2 版）
> 解剖脚本：`tests/mike_anatomy.py`（骨骼/约束/属性 dump）、`tests/mike_pose_probe.py`（IK/FK 行为实验）

## 1. 结论先行（一句话）

ARP 不是"IK 开关"，而是**同一副蒙皮骨上同时挂着 FK 链和 IK 链两套约束**，用一个自定义属性 `ik_fk_switch`（0=IK，1=FK）通过 driver 控制两套约束的影响度；切换模式时用 **Snap** 把当前姿势从一套链复制到另一套链，保证不弹跳。

## 2. 骨骼组织（以左臂为例，`.r` 同理）

### 2.1 蒙皮骨（实际驱动网格，无前缀）

```
c_shoulder.l ── arm.l ── c_stretch_arm.l
                   └── forearm.l ── hand.l ── 手指
```

蒙皮骨上各挂一组成对约束：

| 约束名 | 类型 | 目标 | 影响度 driver |
|---|---|---|---|
| `rotFK` | COPY_ROTATION | `arm_fk.l` | `ik_fk_switch` |
| `rotIK` | COPY_ROTATION | `arm_ik.l` | `1 - ik_fk_switch` |
| `scaleFK` / `scaleIK` | COPY_SCALE | FK/IK 对应骨 | 同上 |
| `locFK` / `locIK` | COPY_LOCATION（前臂/小腿） | FK/IK 对应骨 | 同上 |
| `Stretch To` | STRETCH_TO | IK 骨 | `ik_fk_switch`（伸缩跟随 IK） |

> 注意：`rotIK` 的 driver 表达式实际是 `1 - var`（实验已确认），解剖 dump 里显示 `0` 只是该 fcurve 的缓存值。

### 2.2 FK 链（控制器 + FK 骨）

```
c_arm_fk.l ── arm_fk.l
   └── c_forearm_fk.l ── forearm_fk.l
         └── c_hand_fk_scale_fix.l ── c_hand_fk.l
```

- `c_` 前缀 = 控制器，用户直接操作；同名无 `c_` 的骨（`arm_fk.l`）跟随控制器。
- `c_arm_fk.l` 用 COPY_LOCATION 钉在 `c_shoulder.l` 上，纯 FK 旋转。
- `c_hand_fk.l` 上有 `fingers_grasp` 等手部属性。

### 2.3 IK 链（IK 求解链 + 目标 + pole）

```
arm_ik.l ── forearm_ik.l ── forearm_ik_nostr.l
c_hand_ik.l（IK 目标控制器，CHILD_OF 挂在 c_traj 上）
c_arms_pole.l（肘部 pole，CHILD_OF 挂在 root 上）
```

- `forearm_ik.l` / `forearm_ik_nostr.l` 各有一个 Blender 原生 IK 约束：
  - 目标 = `c_hand_ik.l`，pole = `c_arms_pole.l`
  - 一普通一 nostr（no-stretch），配合 `auto_stretch`/`stretch_length` 属性做伸缩
- `arm_ik.l` 用 COPY_ROTATION 跟随 `c_shoulder.l`。
- 腿同理：`thigh_ik.l → leg_ik.l → foot_ik_target.l`（目标挂 `c_foot_01.l` 下），pole = `c_leg_pole.l`，`c_foot_ik.l` 是脚部 IK 总控制器。

### 2.4 属性位置

- `ik_fk_switch`、`auto_stretch`、`stretch_length` 都挂在 **IK 控制器** `c_hand_ik.l` / `c_foot_ik.l` 上。
- 腿部还有 `fix_roll`（`foot_pole.l` 位置 driver）、`leg_pin`（`c_stretch_leg.l` 约束影响度）等。

## 3. 行为实验（`tests/mike_pose_probe.py` 实测）

### 旋转 `c_arm_fk.l` 90°

| 模式 | FK 链 | 蒙皮骨（arm.l/forearm.l/hand.l） |
|---|---|---|
| IK（switch=0） | 跟随旋转 | **不动**（手被钉住） |
| FK（switch=1） | 跟随旋转 | **完全跟随** |

### 移动 `c_hand_ik.l`

| 模式 | IK 链 | 蒙皮骨 |
|---|---|---|
| IK（switch=0） | 跟随移动 | **手臂整条跟随目标** |
| FK（switch=1） | 跟随移动 | **不动** |

> 这就是 ARP 的行为模型：**FK = 自由摆姿势，IK = 落地/撑墙钉住末端**。

## 4. Snap（切换防弹跳）实现

源码：Mike 附带的 `rig_tools/rig_functions.py`。

### 4.1 核心数学：`get_pose_matrix_in_other_space`

```python
rest = pose_bone.bone.matrix_local
par_mat  = pose_bone.parent.matrix          # 父骨当前姿态
par_rest = pose_bone.parent.bone.matrix_local
smat = rest.inverted() @ (par_rest @ (par_mat.inverted() @ target_mat))
```

含义：给定目标世界矩阵，反推出"在当前父骨姿态下，本骨 local 变换该是多少"。

### 4.2 FK→IK 切换（当前姿势来自 IK，切到 FK 控制）

1. 同步 `stretch_length`（auto_stretch 开启时按骨长比换算）。
2. `snap_rot(arm_fk, arm_ik)`：把每个 FK 控制器的 local 旋转设成"匹配 IK 链当前世界姿态"的值。
3. `hand_fk.scale = hand_ik.scale`。
4. `ik_fk_switch = 1`。

### 4.3 IK→FK 切换（当前姿势来自 FK，切到 IK 控制）

1. 同步 `stretch_length`。
2. 清空 IK offset / foot_01 / toes_pivot / foot_roll 的旋转位移。
3. `hand_ik.matrix = hand_fk.matrix`（有 CHILD_OF 约束时用 `parent.matrix_channel.inverted() @ hand_fk.matrix` 补偿）。
4. 重置自定义 pole 角（`c_arm_ik.rotation_euler[1] = 0`）。
5. 重新算 pole 位置：`snap_pos_matrix(pole, pole_mat)`（把 IK 链当前肘/膝朝向投影到 pole 平面）。
6. `ik_fk_switch = 0`。

## 5. 对我们项目的启示（为什么之前"一拉就回弹"）

之前游戏端的问题是：**直接在蒙皮骨（hand/leg 等）上写 local 变换**，但同一帧里 IK 解算/动画系统又把这些骨覆盖回去，于是"绑死回弹"。

ARP 的正确做法是三层分离：

1. **输入层**：FK 控制器（逐骨旋转）或 IK 目标（末端位置 + pole）。
2. **求解层**：FK 链 = 纯父子旋转；IK 链 = Blender 原生 IK 约束。
3. **蒙皮层**：蒙皮骨只挂"FK 约束 + IK 约束"两组 COPY，用 `ik_fk_switch` 驱动影响度。

对桥接方案意味着：

- 游戏姿态应写回 **FK 链** 或 **IK 目标**（不直接写蒙皮骨）；
- 切换模式必须先 Snap，否则弹跳；
- 我们不需要实现 IK 解算器——Blender 原生 IK 约束就是求解器，桥接端只要按 ARP 命名/约束模板把骨架搭出来即可。

## 6. 参考资源

- ARP 行为文档：<https://www.lucky3d.fr/auto-rig-pro/doc/rig_behaviour_doc.html>
- Mike 演示绑定（官方免费）：<http://lucky3d.fr/auto-rig-pro/mike.zip>
- 解剖数据（本次生成的临时文件）：`%TEMP%\mike_anatomy.txt`、`%TEMP%\mike_probe.txt`

## 7. 插件源码补充（v3.75.14）

> 用户本地有 ARP 3.75.14。官方 `LICENSE.txt` 声明：**插件源码按 GPL-3.0 发布，.blend 资源按 Royalty Free/CC0 发布**，读源码无授权问题。

### 7.1 骨架是怎么"造"出来的

- 手臂/腿的 FK/IK 模块（含 IK 约束、pole、控制器）不是 Python 里现写的，而是从 `limb_presets/modules.blend` 等预置绑定 **拼装** 进主骨架的。所以主构建器 `auto_rig.py` 里看不到"创建手臂 IK 约束"的代码（只有腿的 3 骨变体在 26853 行附近直接 `constraints.new('IK')`）。
- 生成后的约束名与 Mike 里的简称不同：构建器内部用 `Copy Rotation_FK` / `Copy Rotation_IK` / `Copy Scale_FK` / `Copy Scale_IK`（如 26899-26941 行），生成的成品骨架会短命名成 `rotFK`/`rotIK` 等。
- `c_hand_ik.l` 可选带一个 **IK offset 控制器**（`c_ik_offset`，父级是 `c_hand_ik`）：开启后，`forearm_ik`/`forearm_ik_nostr` 的 IK 约束和蒙皮骨 `hand.l` 的 `rotIK` 全部改指向 `c_ik_offset`。手部姿态微调就调它，不破坏 IK 目标。

### 7.2 生成 switch 驱动的标准配方（`src/lib/drivers.py`）

```python
def add_driver_to_prop(obj, dr_dp, tar_dp, array_idx=-1, exp="var", multi_var=False):
    if obj.animation_data is None:
        obj.animation_data_create()
    dr = obj.animation_data.drivers.find(dr_dp, index=array_idx)
    if dr is None:
        dr = obj.driver_add(dr_dp, array_idx)
    var = dr.driver.variables.get('var')
    if var is None:
        var = dr.driver.variables.new()
    var.name = 'var'
    var.type = 'SINGLE_PROP'
    var.targets[0].id = obj
    var.targets[0].data_path = tar_dp   # 例：pose.bones["c_hand_ik.l"]["ik_fk_switch"]
    dr.driver.expression = exp           # 例：var（FK）或 1-var（IK）
```

多变量版（`multi_var=True`）把 `tar_dp` 换成 `{'viz': ..., 'ikfk': ...}` 字典，逐个建变量，再写表达式如 `viz*(1-ikfk)`。

### 7.3 对我们桥接的意义

- 桥接端不需要"发明"IK 求解：Blender 原生 IK 约束 + 预置模块的结构就是解法。
- 生成双链时按此配方写 driver 即可：`rotFK.influence = var`、`rotIK.influence = 1-var`（var 指向 `c_hand_ik.l["ik_fk_switch"]`）。
- 手部如果要"末端微调不破坏 IK"，参考 `c_ik_offset` 层级。
