<div align="center">

# YuzuBiosPatcher

UEFI runtime memory patcher - patch firmware modules in memory from a bootable USB drive. No SPI flashing.

UEFI 运行时内存修补工具 - 可从可启动 USB 驱动器直接修补已加载到内存中的固件模块，无需 SPI 刷写。

**English** / **中文**

Author: **PhorusYu** · [Bilibili](https://space.bilibili.com/1869564846) · [GitHub](https://github.com/PhorusYu)

作者：**PhorusYu** · [Bilibili](https://space.bilibili.com/1869564846) · [GitHub](https://github.com/PhorusYu)

[Features](#features--功能特性) · [Install](#install--安装) · [Usage](#usage--使用方法) · [Build from source](#build-from-source--源码编译)

[功能特性](#features--功能特性) · [安装](#install--安装) · [使用方法](#usage--使用方法) · [源码编译](#build-from-source--源码编译)

</div>

## Features / 功能特性

- **Patch firmware in memory** - find and replace inside loaded UEFI modules; changes last for the current boot only.
- **修补运行中的固件** - 直接修改已加载到内存中的 UEFI 模块；修改仅在当前启动周期内生效。
- **Wildcard patterns** - `..` matches any byte, matched by a regular expression engine (`Oniguruma.efi`).
- **通配符模式** - `..` 表示任意一个字节，实际由正则表达式引擎处理（`Oniguruma.efi`）。
- **Flexible targeting** - select modules by UI name (`Loaded`) or by firmware-volume file GUID (`NonamePE`/`NonameTE`), load images from disk or firmware volumes.
- **灵活定位目标** - 可按模块 UI 名称（`Loaded`）或固件卷中的文件 GUID（`NonamePE` / `NonameTE`）指定目标，也能从磁盘或固件卷中加载镜像。
- **Two patch engines** - `FastPatch` for plain hex find and replace, `Patch` for wildcard patterns with every match patched.
- **双重修补引擎** - `FastPatch` 适用于纯十六进制查找与替换；`Patch` 适用于通配符模式，并会修补所有匹配项。
- **Protocol compatibility layer** - `Op Compatibility` switches the firmware-volume protocol for HP Insyde / Aptio variants.
- **协议兼容层** - `Op Compatibility` 用于切换 HP Insyde / Aptio 系列平台所使用的固件卷协议。
- **Self-contained build** - compiler and headers bundled in the repository; output is a ready-to-copy USB kit.
- **自包含构建** - 编译器和头文件均打包在仓库中；构建结果可直接复制到 USB 驱动器中使用。

## Install / 安装

Build the project (see [Build from source](#build-from-source--源码编译)) and copy everything in `out\` to the root of a FAT32 USB drive, keeping the `EFI\` folder:

编译该项目（见 [源码编译](#build-from-source--源码编译)），然后将 `out\` 中的全部内容复制到 FAT32 USB 盘的根目录，并保留 `EFI\` 文件夹：

| File / 文件 | Purpose / 作用 |
| --- | --- |
| `EFI\Boot\BOOTX64.efi` | UEFI Shell boot loader / UEFI Shell 引导加载器 |
| `YuzuBiosPatcher.efi` | Main program / 主程序 |
| `Yuzu_Config.cfg` | Patch script / 修补脚本 |
| `Oniguruma.efi` | Regular expression engine for wildcard patterns / 通配符模式所需的正则引擎 |
| `startup.nsh` | Auto-start script / 自动启动脚本 |

Boot from the USB drive with **Secure Boot disabled**. The UEFI Shell runs `startup.nsh`, which finds and starts the patcher.

确保 **Secure Boot 已关闭** 后从 USB 启动，UEFI Shell 会执行 `startup.nsh`，随后自动定位并启动修补程序。

## Usage / 使用方法

The patch script `Yuzu_Config.cfg` is a list of commands, one argument per line; `#` starts a comment:

补丁脚本 `Yuzu_Config.cfg` 是按行记录的命令列表，每行一个参数；`#` 表示注释：

```text
Op Loaded
Setup
Op Patch
Pattern
000000FFFF00....0A821206......000F0F
000000FFFF0010270A8247
Op End
```

| Command / 命令 | Argument / 参数 | Action / 作用 |
| --- | --- | --- |
| `Op LoadFromFS` | file name / 文件名 | Load an EFI image from a partition / 从分区加载 EFI 镜像 |
| `Op LoadFromFV` | UI section name / UI 区段名 | Load an EFI image from the firmware volume / 从固件卷加载 EFI 镜像 |
| `Op Loaded` | UI name / UI 名称 | Target an already-loaded module / 目标指定已加载模块 |
| `Op NonamePE` / `Op NonameTE` | file GUID / 文件 GUID | Target a loaded PE/TE module that has no UI name / 指向没有 UI 名称的已加载 PE/TE 模块 |
| `Op LoadGUIDandSavePE` | file GUID / 文件 GUID | Load by GUID and dump the section to `.bin` / 按 GUID 加载并将区段导出为 `.bin` |
| `Op LoadGUIDandSaveFreeform` | file GUID + section GUID / 文件 GUID + 区段 GUID | Extract a FREEFORM/RAW section / 提取 FREEFORM/RAW 区段 |
| `Op Compatibility` | protocol GUID / 协议 GUID | Switch the firmware-volume protocol used afterwards / 切换后续使用的固件卷协议 |
| `Op Patch` | `Pattern` + find + replace / `Pattern` + 查找 + 替换 | Wildcard patch (`..` = any byte), every match / 通配符修补（`..` = 任意字节），匹配全部替换 |
| `Op FastPatch` | `Pattern` + find + replace / `Pattern` + 查找 + 替换 | Exact hex find and replace / 精确十六进制查找并替换 |
| `Op Patch` / `Op FastPatch` | `Offset` / `RelPosOffset` / `RelNegOffset` + offset + bytes / `Offset` / `RelPosOffset` / `RelNegOffset` + 偏移量 + 字节 | Write at an offset / 在指定偏移位置写入 |
| `Op Exec` | - | Start the previously loaded image (e.g. the Setup screen) / 启动先前已加载的镜像（例如 Setup 界面） |
| `Op Skip` | N | Skip the next N commands when the previous one succeeded / 在前一条命令成功时跳过接下来的 N 条命令 |
| `Op UninstallProtocol` | protocol GUID / 协议 GUID | Uninstall a protocol from every handle / 从所有 handle 中卸载指定协议 |

## Build from source / 源码编译

Everything needed ships with the repository - a trimmed GCC (`toolchain\`) and UEFI headers (`edk2-include\`). On Windows:

仓库中已自带所需工具链：精简版 GCC（`toolchain\`）和 UEFI 头文件（`edk2-include\`）。在 Windows 上执行：

```bat
build-lite.bat          & rem USB kit in out\
build-lite.bat DEBUG    & rem debug build
```

## Compatibility / 兼容性

Tested models and firmware

已验证的机型：

| Model / 机型 |
| --- | --- |
| MECHREVO Yaoshi 16 Ultra (X6AR_Mechrevo-Yaoshi-16-Ultra) |
| XMG NEO 16 E25 Intel (XNE16E25) |

## Disclaimer / 免责声明

This tool patches firmware modules in memory. Misuse may cause setup-screen glitches, hangs or data loss. Use at your own risk; the author is not liable for any damage.

本工具会直接修补位于内存中的固件模块。误用可能导致设置界面异常、系统卡死或数据丢失。使用风险由使用者自行承担；作者不对任何损害负责。

## Acknowledgments / 致谢

- [barlowhaydnb/SmokelessRuntimeEFIPatcher](https://github.com/barlowhaydnb/SmokelessRuntimeEFIPatcher) - the project this is based on / 本项目的参考基础
- [EDK2](https://github.com/tianocore/edk2) - UEFI headers and the RegularExpressionDxe (Oniguruma) engine / UEFI 头文件与 RegularExpressionDxe（Oniguruma）引擎
