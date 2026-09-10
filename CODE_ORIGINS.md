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

Lua 固定为 5.5.1，与 DESIGN.md、plan.md 和 `ref/lua-5.5.1/` 的来源锁一致。
本阶段没有链接 Lua、libffi、LVGL。
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

## BIOS 高内存设计参考

- `ref/grub4dos/stage2/builtins.c`：`map --mem --top` 的高内存选择。
- `ref/grub4dos/stage2/asm.S`：`int13_paemove` / `int13_lm64move_lm64_start` 的窗口搬运与模式切换。
- `ref/wimboot/src/paging.c`、`src/main.c`：`relocate_memory_high()` 的 PAE / 2 MiB 窗口搬运、原虚拟地址重映射、INT 13h callback 分页切换及 Windows 启动交接处理。
- `ref/syslinux-6.04-pre1/memdisk/setup.c`、`memdisk.inc`：驻留/E820/BIOS 传输；
  `setup.c` 明确将地址限制在低 4 GiB，不能将其视为高地址实现。
- `ref/grub/include/grub/i386/linux.h`：Linux initrd 地址限制、扩展字段及能力标识。

Phase 1 的高内存基础实现见下；initrd loader 和驻留 map 仍属于 Phase 7、8、9。

## Phase 1 迁移与新实现（2026-09-09）

迁移前已运行 `python3 tools/check_references.py`，全部内容摘要匹配；没有改动 ref/ 或来源锁。

| 目的文件 | 来源文件和固定提交 | 版权与适配差异 |
| --- | --- | --- |
| platform/bios/physical.c | ref/wimboot/src/paging.c，e7fab3ca8caba24e05280b6e0869898267cac057 | 保留 Michael Brown 2021 版权，按 or-later 使用 GPLv3；适配 PAE 初始化和 CR0/3/4 切换。新加所有权/位宽检查、共享分块、分页关闭入口约束及低地址 bounce；没有沿用原地址永久重映射。 |
| platform/bios/entry.S、core/context.c | ref/grub/include/multiboot2.h，2f972128c48b90bf8b63aadffe6d546976e1dee6 | 基于标准布局新写；沿用本项目 Phase 0 的整数 CPU gate 和 eager FP 初始化。 |
| platform/bios/linux_setup.S、tools/linux_image.py、core/context.c | ref/grub/include/grub/i386/linux.h，同上 | 核对 Linux header/boot_params 偏移；新写 setup/E820/protected-mode adapter，使用同一 CMake payload。 |
| include/boot/efi.h、core/efi_memory.c、platform/efi/ | ref/grub/include/grub/efi/api.h，同上 | 核对标准表布局、memory types 与 ABI，未复制完整 EFI runtime。 |
| platform/bios/resident.S、resident.c | INT 15h E820 接口及 DESIGN.md §7.1 | 新写 CS 相对驻留 handler、冻结图及真实模式回读探针，未移植 GRUB4DOS map。 |
| core/memory.c、console.c、physical.c；tools/loongarch_pe.py | 本项目新实现 | 所有权事务、日志、共享分块和严格限定的构建期 PE 转换；runtime PE 仍由固件加载。 |

QEMU/EDK2 是 host 测试依赖，下载和构建仅在 build/ 内，不链接进产品。
固定版本/摘要记录在 tools/prepare_phase1.py；ARM64/LoongArch Resident 粒度核对 EDK2
MdePkg/Include/{AArch64,LoongArch64}/ProcessorBind.h 的 64 KiB 约束。
LoongArch 的实际 EDK2 入口 EUEN.FPE=0 异常由新写汇编 eager FP 入口解决。

验证见 tests/phase1.py、tests/gcc_smoke.py、docs/phase1.md 和 progress.md；
不把上述基础实现视为 Phase 7/9 的 initrd 协议交接或 INT 13h map 已完成。

## Phase 2 模块 ABI/SDK（2026-09-10）

`core/module.c`、`include/boot/module.h`、`platform/module.c`、`sdk/`、模块测试和嵌入工具
均为本项目新写，使用 SPDX GPL-3.0-or-later；没有从 ref/ 迁移代码。
沿用 `core/elf.c` 的标准 ELF 受限 profile，新增项目自有 BOOTMOD note 和 C ABI。
迁移检查命令 `python3 tools/check_references.py` 已通过；ref/ 与来源锁保持不变。
实际调用和跨架构边界见 `docs/phase2.md`、`tests/phase2.py` 和 `progress.md`。

## Phase 3 资源归档（2026-09-10）

迁移前 `python3 tools/check_references.py` 全部通过；ref/ 和来源锁未修改。

| 目的文件 | 参考及固定提交 | 版权与适配差异 |
| --- | --- | --- |
| `core/archive.c` | `ref/grub/grub-core/fs/newc.c`、`cpio_common.c`，`2f972128c48b90bf8b63aadffe6d546976e1dee6` | 核对公开 newc 布局、4 字节对齐和 trailer；新写有界内存 reader，没有复制上游实现。严格 hex/范围/路径/类型检查；不引入 disk/archelp、全局错误或模块注册/卸载。 |
| `core/sha256.c` | 标准 SHA-256 算法 | 本项目新写；已知答案及 Python hashlib 独立比较，未从参考树迁移实现。 |
| `platform/resource.c`、`include/boot/{archive,resource}.h`、packer 和测试 | 本项目设计 | 使用已有 context/物理内存/模块接口；新写 manifest 校验、BL 副本和内嵌 section 适配。 |
| `resources/boot.lua`、`resources/fonts/minimal.hex` | 本项目新写/绘制 | GPL-3.0-or-later；5×7 glyph 为最小诊断子集，没有第三方字体数据。 |

上述新代码均标记 SPDX GPL-3.0-or-later。未迁移完整磁盘文件系统；支持 profile、资源
输入和执行证据见 `docs/phase3.md`、`tests/phase3.py` 和 `progress.md`。
