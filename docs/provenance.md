# 本地输入与来源记录

本文件记录可复现实验所需、但不能提交进 Git 的输入。哈希是身份门禁，不代表
这些文件可以重新分发。

## 日版 ROM

- 文件位置：本地 `rom.z64`，不提交；
- 大小：33,554,432 字节；
- SHA-256：`ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`；
- 字节序 magic：`80371240`；
- 游戏代码/修订：`NS4J`, Rev 0；
- 头部 CRC：`1649d810 f73ad6d2`。

## 韩版参考项目

- 本地位置：`ref-project/`，作为独立 Git 克隆并由父仓库忽略；
- 上游：`https://github.com/snowyegret23/Localize.git`；
- 分支：`main`；
- 固定提交：`91e0c15b76b44c302f29ddebc1c45e61f1828cd0`；
- 用途：SRW N64 文本表和字库格式 oracle，不作为构建时依赖。

重新准备时使用：

```sh
git clone https://github.com/snowyegret23/Localize.git ref-project
git -C ref-project checkout 91e0c15b76b44c302f29ddebc1c45e61f1828cd0
```

## 中文字体

- 本地名称：`RODE Noto Sans CJK SC R.otf`，不提交；
- W1 门禁 SHA-256：
  `5123c624dae4a41715a3fc9d6aaf1cf0e3973eafd6ee9adfa51c9ee04de76ebb`。

## Libretro 核心

- 核心：Mupen64Plus-Next arm64；
- 获取日期：2026-08-02；
- 来源：`https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/mupen64plus_next_libretro.dylib.zip`；
- dylib SHA-256：
  `8cd7541261b06b89c18189d7621b825e4e6f906b64f4449056a40d0647a6f58d`；
- 本地位置：`build/libretro/cores/mupen64plus_next_libretro.dylib`。

`latest` 地址会漂移；任何不同哈希都应当作为新的运行时环境重新验证，不能沿用
当前截图或存档结论。
