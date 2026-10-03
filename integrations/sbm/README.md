# SBM 的 MMD 协作构建

Poser 已内置 **第二骨骼物理增强**，仅用于 MMD 动作播放，无需另装 SBM。使用方法见 [播放指南](../../docs/mmd-player.md#衣物物理)，骨骼表来源见 [来源说明](NOTICE.md)。

此目录保留可选协作构建，供需要同时安装外置 SBM 的用户使用。Poser 接管期间会暂停对应角色的 SBM 写入；关闭 Poser 增强后交还 SBM。内置增强的开关和参数由 Poser 控制。

## 构建与安装

1. 准备 [SBM 源码](https://github.com/Sp1cHless/Arknights-Endfield-Plugin-Secondary-bodyphysics/tree/f2ee3f0380827d573cab3cbd163781438cd56937)，使用 `Also-1.4` 对应的提交。该协作补丁尚不支持其他版本。
2. 安装 MSVC x64 与 Windows SDK，在 Poser 源码目录运行：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_sbm_mmd.ps1 -SourceDir "SBM 源码目录"
   ```

3. 完全退出游戏，备份游戏 `plugin/sbm.dll`，用 `build/sbm-mmd/sbm.dll` 替换。保留 SBM 的 `SecondaryMotion` 配置与预设。如果管理器目录内也有 `plugin/sbm.dll`，一并备份更新，以免管理器覆盖回旧版。
4. 使用支持此功能的 Poser，在 **动作 → 衣物物理 → 第二骨骼物理增强** 调整强度。多人面板的 **站位与适配** 中也有相同设置。

构建脚本校验固定版本的源文件，输出单独 DLL，不安装或替换游戏加载器。回退时退出游戏，再还原备份的 `sbm.dll`。

## 源码与许可

SBM 来源于 [Sp1cHless/Arknights-Endfield-Plugin-Secondary-bodyphysics](https://github.com/Sp1cHless/Arknights-Endfield-Plugin-Secondary-bodyphysics)，遵循其 GPL-3.0 许可证。构建目录保留原项目的 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`；发布协作版 DLL 时须同时提供对应源码及本目录的补丁构建材料。Poser 不包含 SBM 管理器。
