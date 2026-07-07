# NTE Remove Blur Tool

UE 5.6 编辑器插件，用于一键给角色动画蓝图生成节点，尝试移除指定材质槽的近镜头点阵虚化 / 透明裁切效果。

## 功能

- 选择一个 Animation Blueprint。
- 输入一个或多个 Material Slot ID，例如 `3` 或 `1,5,6`。
- 自动创建每个材质槽对应的 Dynamic Material Instance 变量。
- 在 `BlueprintInitializeAnimation` 和 `BlueprintUpdateAnimation` 中写入参数覆盖节点。
- 默认反虚化参数：
  - `FPSCameFade = 0`
  - `FPSCameraCutHeight = -99999`
  - `FPSCameraCutHeightOverride = -99999`
  - `PlayerDitherRandom = 0`
  - `OpacityMaskVal = 1`
  - `MaskOpacity = 1`
  - `InvisableSkillOpacity = 1`
  - `PlayerInvisable = 1`
  - `Opacity = 1`
  - `Dissolve = 0`
  - `DissolveSkill = 0`

## 安装

1. 下载本仓库。
2. 将 `NTERemoveBlurTool` 文件夹放入 UE 工程的 `Plugins` 目录。
3. 启动 UE 5.6 工程。
4. 在插件管理器中启用 `NTE Remove Blur Tool`。

仓库中已包含 UE 5.6 Win64 编译好的 `Binaries`，正常情况下不需要重新编译。

## 使用

1. 打开菜单 `Tools > NTE Remove Blur Tool`。
2. 选择角色的 Animation Blueprint。
3. 填入需要处理的材质槽 ID。
4. 点击 `Generate Remove Blur Nodes`。
5. 进入游戏测试对应角度或近距离是否还会虚化。

## 注意

这个插件适用于虚化逻辑暴露为材质标量参数的情况。如果父级材质图中把 `DitherTemporalAA`、`HT_DitherMask` 或类似遮罩逻辑写死，并且完全不读取这些参数，那么仅靠蓝图不能彻底移除，需要修改或替换父级材质。
