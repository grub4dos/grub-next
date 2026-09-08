# 代码来源与迁移基线

项目整体使用 GPL-3.0-or-later，完整许可文本见 LICENSE。新写的 Phase 0 C/汇编代码
使用 SPDX 标识。除许可文本外，本阶段没有把参考 runtime 源码复制进产品，也没有把整个
GRUB 构建作为新项目的一部分。

## 可审计参考历史

采用 plan.md 允许的“可审计迁移历史”路线。`references.lock.json` 记录本地 ref/ 的
远端、提交、Git tree、文件数及 SHA-256 内容清单摘要；Git 参考要求已跟踪文件无修改。
无 Git 元数据的发布目录按文件内容锁定。ref/ 不随新项目提交、不参与 Phase 0 构建。
干净克隆运行 Phase 0 不需要参考树；需要迁移时从锁中的 remote 获取指定 commit，
放入对应 ref/ 目录，再执行：

```sh
python3 tools/check_references.py
```

该命令对缺失、修改或不同提交返回失败。显式 `--record` 仅供审核过的基线更新使用。
摘要算法为：路径按字典序排序，逐条拼接 UTF-8 相对路径、NUL、文件内容 SHA-256（二进制），
最后对拼接流取 SHA-256；符号链接使用链接目标文本。Git 未跟踪文件不作为迁移输入；
子模块由 Git tree 的 gitlink 锁定，真正迁移子模块文件时需另加其独立来源记录。

| 参考树 | 固定提交 | 许可基线 |
| --- | --- | --- |
| `grub` | `2f972128c48b90bf8b63aadffe6d546976e1dee6` | GPL-3.0-or-later |
| `grub4dos` | `cd65de1e1a1fda15de8cc45d09ffa0cfaca9aa7c` | GPL-2.0-or-later |
| `ipxe` | `ff6e52063e0b37062394fe37b9788af25175e7af` | 迁移时按文件审查；未导入 |
| `libffi` | `12ffd1f9dc56fcea79d2f742f424301ae668d663` | MIT |
| `lua-5.5.1` | `非 Git 发布目录` | MIT |
| `lvgl` | `85aa60d18b3d5e5588d7b247abf90198f07c8a63` | MIT（第三方组件另审） |
| `ntloader` | `744a6108d32ea47df95e9fd1ab42884525685985` | GPL-3.0-or-later |
| `osloader` | `7278a618349f5bd1914b17c8bd4f5bccec823ed7` | 迁移时按文件审查；未导入 |
| `syslinux-6.04-pre1` | `非 Git 发布目录` | 迁移时按文件审查；未导入 |
| `wimboot` | `e7fab3ca8caba24e05280b6e0869898267cac057` | GPL-2.0-or-later |

Lua 输入为 5.5.1，不能满足 DESIGN.md 的 Lua 5.4 要求。Phase 6 必须取得并固定 5.4.x，
或经用户明确决定修改设计；不能静默替换。本阶段没有链接 Lua、libffi、LVGL。
LVGL 内嵌第三方组件等不因顶层许可而自动归入 MIT，正式迁移逐文件审核。

## 本阶段参考位置与结果

| 新工程文件 | 参考位置 | 使用方式 |
| --- | --- | --- |
| LICENSE | ref/grub/COPYING | 原样复制 GPLv3 完整许可文本 |
| platform/efi/hello.c、tests/efi_parent.c | ref/grub/grub-core/kern/x86_64/efi/startup.S、ref/grub/include/grub/efi/api.h | 核对 x64 EFI 调用约定和标准表布局；实现为新代码 |
| platform/bios/start.S、linker/i386-pc.ld | ref/grub/include/multiboot2.h | 核对 Multiboot2 标识；新写最小 header/GDT/栈/FP 入口 |
| GRUB4DOS 兼容性边界 | ref/grub4dos/stage2/boot.c | 本地参考包含 Multiboot1 loader，未提供 Multiboot2 直接加载证据 |
| 后续 Windows loader 来源 | ref/ntloader/kern/main.c、ref/wimboot/src/main.c | 已核对文件头的 or-later 条款；本阶段未移植 |

后续每次移植必须增加“目的文件 → 源文件及 commit → 保留版权 → 改动摘要 → 验证”记录，
不能把此基线表当作已经完成存储、map 或 Windows loader 移植的证明。
