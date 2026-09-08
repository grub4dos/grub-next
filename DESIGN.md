# New Bootloader Design

## 1. Status

本文档定义新 bootloader 的总体架构、稳定边界和主要设计约束。项目名称、命令名称和磁盘路径语法尚未确定；除这些表层名称外，本文中的架构决策视为基线设计。

规范性用语“必须”“不得”“应当”“可以”分别对应 MUST、MUST NOT、SHOULD、MAY。

## 2. Goals

项目目标是以 GNU GRUB 2 的成熟存储栈为基础，重新构建一个更小、更清晰、适合现代多启动和系统恢复用途的 bootloader。

核心目标如下：

- 只支持仍有实际用途的 x86、ARM64 和 LoongArch64 平台。
- 保留 GRUB2 的 disk、partition、filesystem、cryptodisk 和 diskfilter 等成熟实现。
- 删除 GRUB2 自有脚本 parser、旧菜单系统、主题系统、native disk driver 和非目标操作系统 loader。
- 使用 Lua 统一配置、菜单生成、自动探测和调试接口。
- 使用 LVGL 实现图形界面，同时保留独立文本前端。
- 使用无依赖、不可卸载的标准 ELF 模块。
- 将 GRUB4DOS 的 BIOS 和 UEFI `map` 能力作为一等功能。
- 将固件接口和可加载驱动作为硬件能力来源，bootloader core 不发展成通用硬件驱动框架。
- 初期将 BIOS 版本作为可由现有 loader 加载的 stage2，随后再补充原生 MBR/PBR 启动链。
- 提供稳定、可由源码树外模块使用的 SDK 和 ABI。

## 3. Non-goals

下列内容不属于项目目标：

- 兼容现有 `grub.cfg`、GRUB shell 或 GRUB module ABI。
- 保留专用 BSD、XNU、Hurd 或其他历史操作系统 loader。
- 在 EFI 环境中模拟完整 BIOS、CSM 或 hypervisor。
- 在 core 中实现完整 TCP/IP、DHCP、DNS、TLS、HTTP 或 TFTP 协议栈。
- 支持模块卸载、模块间依赖、抢占式调度、SMP 或多线程 Lua。
- 支持下一级 bootloader 或操作系统返回菜单。
- 默认提供通用文件系统写入能力。

## 4. License and provenance

项目整体采用：

```text
GPL-3.0-or-later
```

GNU GRUB 2 代码按其 GPLv3-or-later 条款使用。GRUB Legacy 和 GRUB4DOS 的 GPLv2-or-later 代码可以升级并组合到 GPLv3-or-later 项目中。项目维护者已具备修改相关 GRUB4DOS 代码的授权。

每个派生文件应保留原始版权信息并使用 SPDX 标识。项目应维护 `CODE_ORIGINS.md`，记录各子系统来源，例如：

| 子系统 | 主要来源 |
| --- | --- |
| disk/filesystem/diskfilter | GNU GRUB 2 |
| BIOS map | GRUB4DOS |
| EFI Block I/O map | GRUB4DOS |
| Windows loader | GRUB4DOS、NTloader、wimboot |
| module/Lua/UI | 新项目 |

## 5. Supported platforms

正式 target 如下：

| Target | Firmware | Native word size |
| --- | --- | ---: |
| `i386-pc` | BIOS | 32 bit |
| `i386-efi` | UEFI | 32 bit |
| `x86_64-efi` | UEFI | 64 bit |
| `arm64-efi` | UEFI | 64 bit |
| `loongarch64-efi` | UEFI | 64 bit |

“Loongson EFI”在本项目中指 LoongArch64 EFI，不包含旧 MIPS Loongson 平台。

### 5.1 Feature matrix

| 功能 | i386-pc | i386/x86_64 EFI | ARM64/LoongArch64 EFI |
| --- | ---: | ---: | ---: |
| EFI application chainload | 否 | 是 | 是 |
| BIOS sector chainload | 是 | 否 | 否 |
| NTLDR/DOS/FreeLdr | 是 | 否 | 否 |
| Linux x86 boot protocol | 是 | 非首选 | 否 |
| Linux EFI stub/UKI | 否 | 是 | 是 |
| Multiboot 1/2 | 是 | x86 可选 | 非目标 |
| BIOS INT 13h map | 是 | 否 | 否 |
| EFI Block I/O map | 否 | 是 | 是 |

### 5.2 Floating-point baseline

所有平台使用 eager hard-float，不采用 lazy FP。x86_64 EFI 以 x86-64-v1 的 SSE/SSE2
为基线；ARM64 EFI 使用 FP/SIMD；LoongArch64 EFI 使用 LP64D。
i386 BIOS/EFI 要求 SSE2，编译使用 `-msse2 -mfpmath=sse`，最低 CPU 为 Pentium 4
同等级特性集合。不默认启用 `-mno-80387`，保留标准 i386 浮点返回约定的兼容空间。

BIOS startup 必须在执行 C/浮点代码前检查 CPUID、FPU、FXSR、SSE、SSE2；
清除 CR0.EM/TS，设置 CR0.MP/NE 和 CR4.OSFXSR/OSXMMEXCPT。缺少特性时走整数错误路径。
UEFI 入口遵循固件架构执行约定。每个正式 handoff 前必须规范化 FP state：
x86 使用 FNINIT 和默认 MXCSR 0x1F80；ARM64 清理 FPCR/FPSR；LoongArch 规范化 FCSR。
其他架构入口及正式 handoff 的实现属于 Phase 1 及 loader 阶段，不由 Phase 0 配置探针代替。

## 6. Execution and lifecycle model

core 采用单 CPU、单线程、轮询式模型。不得引入抢占式 scheduler 或模块并发卸载。

顶层生命周期为：

```text
INIT -> MODULE_CONFIG -> MENU -> LOADER_PREPARE -> HARAKIRI_COMMIT -> HANDOFF
                                                               \-> FATAL
```

### 6.1 INIT

初始化平台、内存、日志、基础终端、模块 loader、资源 archive 和基础 disk provider。

### 6.2 MODULE_CONFIG

加载 ELF 模块和 EFI/BIOS 驱动，执行 Lua 配置，注册 filesystem、filter、loader 和 Lua binding，构造菜单模型。模块和驱动的加载必须在进入 MENU 前结束。

### 6.3 MENU

运行文本或 LVGL 前端，允许设备探测、菜单选择和 Lua REPL。注册表被冻结，不得再注册或卸载模块、文件系统或 loader。

### 6.4 LOADER_PREPARE

loader 加载并验证目标，创建 pending map，分配下一级需要的内存，生成可回滚的 `boot_plan`。本阶段失败必须能够回到 MENU。

### 6.5 HARAKIRI_COMMIT

提交 map，构造并发布 BMIT，冻结所有设备状态，清理不再需要的敏感数据，终止 Lua/UI 活动。此阶段之后不得恢复 MENU。

### 6.6 HANDOFF

调用 EFI `StartImage()`，或跳转 Linux、Multiboot、boot sector、NTLDR、DOS、FreeLdr、bootmgr.exe 或 osloader.exe。下一级代码不得返回。

EFI application 若返回，进入 FATAL 并 reset。EFI driver 的 entry point 返回 `EFI_SUCCESS` 属于正常初始化行为，不受此限制。

## 7. Memory model

只定义三类内存所有权。

| 类别 | 所有者与生命周期 | 内容 |
| --- | --- | --- |
| BL | bootloader 使用，handoff/EBS 后作废 | core、模块、Lua、LVGL、存储对象、cache、scratch |
| Loader | 单次启动准备；失败回滚，成功移交下一级 | kernel、initrd、boot params、Multiboot info、load options |
| Resident | 跨越 handoff，必须显式保留 | BIOS handler、blocklist、memdisk、BMIT |

BL allocator 可以实现普通 `free()` 或 mark/rewind，但这些只是实现细节，不构成新的生命周期类别。

Loader memory 必须支持：

```text
loader_begin -> allocate -> loader_abort
                         \-> loader_commit -> transfer ownership
```

Resident 对象必须自包含，不得引用 BL 或 Loader memory。所有字符串、blocklist 和描述记录必须在 commit 时深复制到 Resident memory。

UEFI BS/RT driver 的 code/data 内存由固件根据 PE subsystem 分配和管理，不属于项目 allocator。

### 7.1 BIOS high physical memory

BIOS `map --mem` 和 initrd 应充分利用 4 GiB（0x100000000）以上的可用物理内存，
不得因 i386 core 的 32 位指针而把物理内存分配统一限制在 4 GiB 以下。
这不增加新的所有权类别：准备启动的 initrd 属于 Loader，跨 handoff 的 memdisk 属于 Resident。

- 物理地址、区域长度及地址运算使用明确的 64 位整数；不得通过 `uintptr_t`、普通 C 指针
  或 Lua double 保存高物理地址。每次范围运算检查溢出、E820 可用范围、保留区和 CPU 物理地址位宽。
- 在具备 PAE 的 CPU 上实现受控映射窗口与分块物理内存读写/搬运，使不支持 long mode 的
  SSE2 CPU 也能使用高内存；long mode 搬运可以作为已检测能力下的优化，不能成为高内存的唯一实现。
  SSE2 不代替 PAE 能力检查。缺少高地址访问能力或可用区域时，回退到满足约束的低地址内存；
  容量不足则在 prepare 阶段返回明确错误，不得截断地址或静默缩小映像。
- bulk data 优先放入高内存，按调用约束保留低地址空间给 BIOS handler、thunk、页表、
  boot parameters 和 bounce buffer。窗口映射只产生短期可用的虚拟地址，不可当作永久物理指针保存。
- BIOS/INT 13h 调用不能直接消费高地址 buffer 时，经符合固件地址/大小约束的 bounce buffer
  分块传输。模式切换和窗口操作必须保持或恢复调用者的 CPU、分页、段寄存器及 FP 状态，
  不得破坏 eager FP 或 BIOS handler 返回约定。
- 分配器、copy、校验和加载路径均须区分“位于 4 GiB 以上”与“数据大小超过 4 GiB”，
  支持跨越 4 GiB 边界的区域；单次映射/传输大小仍按窗口和 provider 限制分块。

`map --mem` 的高地址数据、驻留访问代码、页表、bounce buffer 和必要描述必须在 commit 后
自包含，不引用 BL/Loader 对象；通过统一 E820 handler 保留实际 Resident 区域，并由 BMIT
以 64 位物理地址/长度描述。下一级通过 INT 13h 访问 memdisk 时，无需自身具备高地址指针。

initrd 的高地址暂存能力不代表任意启动协议都能传递高地址。loader 必须检查所选入口、
协议版本、地址/长度字段及目标能力；Linux 必须遵守适用的 `initrd_addr_max`，仅在所选入口
确实支持高地址 initrd 时使用 `ext_ramdisk_image` / `ext_ramdisk_size` 等扩展字段。
否则在 prepare 中搬到协议允许的区域，无法容纳则失败并回滚。上级传入的 initrd/resource
同样按输入协议解析，不得为只有 32 位地址字段的入口假设不存在的高位信息。

参考 `ref/grub4dos/stage2/builtins.c` 的高内存选择及 `stage2/asm.S` 的 PAE/long-mode
INT 13h 搬运。`ref/wimboot/src/paging.c` 的 `relocate_memory_high()` 提供基于 PAE 的
2 MiB 窗口搬运，将 initrd 移到 4 GiB 以上并重映射原虚拟地址；`src/main.c` 在 INT 13h
callback 周围启用/恢复分页，供 Windows 启动过程访问高地址数据。此模式应作为 BIOS
Windows loader / WIM 资源高内存使用的参考，不能泛化成 Linux initrd 的直接高地址交接。
其交接描述处理属于 wimboot 自身约定，新项目仍须按统一 Resident/E820/BMIT 契约保留数据。
`ref/syslinux-6.04-pre1/memdisk` 用于驻留布局、E820 和 BIOS 传输设计参考；
其 `setup.c` 明确限制 32 位地址，不能作为已经支持 4 GiB 以上 RAM disk 的证据。

## 8. Build system

CMake 是唯一构建描述。Makefiles 或 Ninja files 只作为 CMake generator 输出；平台最终布局由 linker scripts 定义。

构建分成：

- host tools；
- target runtime；
- external module SDK；
- resource archive/signing tools。

项目应提供每个 target 的 CMake toolchain file。Clang/LLD 作为主要 CI 工具链，GCC/binutils 作为兼容工具链。BIOS 16 位入口和 thunk 使用独立汇编文件，不将大量 `.code16` 特例混入普通 C。

构建必须可复现：不得写入绝对源码路径和非确定性时间；必须支持 `SOURCE_DATE_EPOCH`；模块、archive 和 manifest 排序必须稳定。

## 9. ELF module model

模块使用标准 ELF `ET_DYN`：

- 所有模块代码必须 PIC。
- 模块不得依赖其他模块。
- 模块不得卸载。
- 模块不得直接引用 core 普通全局符号。
- 模块只能通过传入的版本化 `boot_api` 使用 core 服务。
- 模块只导出一个固定 entry。
- 其他符号使用 hidden visibility。
- 模块不得使用 TLS、系统 libc、异常或未声明的 runtime。
- loader 只支持经过白名单约束的 relocation。

模块状态是单调的：

```text
NOT_LOADED -> LOADING -> ACTIVE
                      \-> FAILED
```

模块初始化注册应具有事务语义。初始化失败时撤销本次尚未提交的注册项，但不执行通用 module unload。

### 9.1 ABI

模块入口接收单一 API table：

```c
const struct boot_module_v1 *boot_module_entry(void);
```

API 通过 `abi_version`、`struct_size` 和 capability bits 协商。模块 metadata 位于固定 ELF note，至少包含：

- vendor/module UUID；
- name/version；
- target architecture；
- ABI range；
- provided capabilities；
- required core capabilities；
- content hash/signature metadata。

“required core capabilities”不是模块依赖，不得引用另一个模块名称。

共享库要么进入 core API，要么静态链接进模块。允许适度重复代码，不建立隐式模块依赖图。

## 10. Resource archive

关键模块和驱动必须能够从自包含 resource archive 加载，以解决驱动位于固件尚不可访问设备上的自举问题。

初期格式采用未压缩 `cpio newc`：

```text
resource.cpio
|-- boot.lua
|-- modules/<arch>/
|-- drivers/efi/<arch>/
|-- drivers/bios/
|-- fonts/
|-- themes/
`-- trust/
```

archive 可以来自：

- EFI image 内嵌 section；
- Multiboot2 module；
- Linux boot protocol initrd；
- 上级 loader 传入的资源；
- 已经可访问的文件系统。

以后可以增加压缩，但解压器必须位于 core，不能依赖模块解压自身。

## 11. Lua configuration and debugging

Lua 完全取代 GRUB parser、GRUB shell、`grub.cfg` 和旧菜单脚本系统。项目不提供旧语法兼容层。

要求如下：

- 固定使用 Lua 5.5.1，与 `ref/lua-5.5.1/` 的来源锁保持一致。
- 只接受 UTF-8 Lua source，不加载 Lua bytecode。
- 禁止标准 `io`、`os` 和任意 native library loading。
- 对内存和指令执行设置限制。
- Lua 构造抽象 menu model，不直接依赖 LVGL widget。
- 配置失败时进入文本错误界面或 Lua REPL。
- 地址、LBA、MSR 等值必须使用 `lua_Integer` 或 userdata，不得经由 double。

### 11.1 FFI

移植 libffi，提供 BIOS 和固件调试能力。初期只实现 outbound call，不实现 closure/callback。

常用硬件操作使用类型安全的专用 API：

- physical memory；
- I/O port；
- MSR；
- PCI configuration；
- BIOS interrupt；
- EFI protocol inspection。

FFI、任意内存写入和其他原始硬件调试接口在 Secure Boot 启用时必须由 C 层禁止，即使用户确认继续、脚本受信任或 FFI 模块已签名。

## 12. UI

UI 分成抽象 menu model 和两个独立 frontend：

- text frontend；
- LVGL frontend。

LVGL 失败时必须能退回文本界面。最低层文本/串口输出不依赖 Lua、LVGL、filesystem 或动态模块。

图形内部格式统一为 XRGB8888/ARGB8888，由 GOP/VBE backend 转换到实际 framebuffer 格式。字符串统一使用 UTF-8。core 内置最小 ASCII 字体，CJK 等字体从 resource archive 按需加载。

## 13. Disk and storage architecture

项目废除 GRUB2 `nativedisk` 及 core 内直接驱动 ATA/AHCI/USB controller 的实现。

存储层次为：

```text
Transport provider
    -> Virtual block layer
    -> Partition/cryptodisk/diskfilter
    -> Filesystem
    -> Loader or map exporter
```

### 13.1 Transport providers

core 只包括：

- BIOS INT 13h HDD/CD/FDD；
- UEFI Block I/O HDD/CD/FDD；
- loopback；
- memdisk/blocklist virtual devices。

附加硬件能力来自：

- UEFI BS/RT drivers；
- iPXE `efidrv`；
- BIOS resident USB/NVMe drivers。

### 13.2 Filters and volumes

保留并适配 GRUB2：

- partition maps；
- cryptodisk；
- diskfilter；
- LVM；
- RAID；
- filesystems。

filesystem 默认只读。持久化少量启动状态时，UEFI 使用项目 GUID variable；BIOS 使用预分配、固定长度、双槽和 CRC 的状态文件。

### 13.3 Block API

底层 API 必须原生支持设备自己的 logical block size、physical block size、alignment 和 maximum transfer size，不得假定 512 字节扇区。byte-range access 由上层 adapter 提供。

每个设备保存 provider lineage 和稳定 identities。`(hd0,gpt1)`只作为临时显示别名，配置应优先使用 GPT GUID、partition GUID、filesystem UUID 或其他稳定 identity。

## 14. UEFI driver loading

EFI driver 与 ELF module 是完全不同的机制。

EFI driver 必须通过固件：

```text
LoadImage -> StartImage -> ConnectController -> protocol rescan
```

不得自行映射或跳转 EFI driver PE image。`LoadImage()`负责 PE/COFF、架构、relocation 和当前固件安全策略校验。

项目只关注 `EFI_LOADED_IMAGE_PROTOCOL` 反映的内存类型：

| Driver | Code/data type | Lifetime owner |
| --- | --- | --- |
| BS | BootServicesCode/Data | firmware, until EBS |
| RT | RuntimeServicesCode/Data | firmware/runtime environment |

成功加载的 driver 不由项目卸载。driver 返回 `EFI_SUCCESS`属于正常初始化。项目随后调用 `ConnectController()`并重新枚举 Block I/O、Simple File System、SNP、PXE、LoadFile 和其他标准协议。

iPXE 网络驱动使用 `ipxe.efidrv`，不是普通 `ipxe.efi` application。

## 15. BIOS driver loading

BIOS 下可加载独立 USB/NVMe resident driver，类似 PLoP Boot Manager。驱动安装 INT 13h，随后 bootloader 重新枚举 BIOS disks。

固定 hook 顺序为：

```text
firmware INT 13h
    -> USB/NVMe transport driver
    -> bootloader map handler
    -> next loader/OS
```

map handler 必须最后安装并位于调用链最上层。

新 BIOS driver 应报告 resident code、DMA buffer、drive range 和其他保留内存。core 应尽可能使用单一 INT 15h E820 handler 统一隐藏 resident regions，避免多个新驱动各自修改 E820。

BIOS NVMe driver 应向旧软件提供兼容的 INT 13h Extensions 接口，并在需要时执行 4Kn/512e sector translation 和低地址 bounce buffering。

## 16. Virtual disk and map

`map` 建立在平台无关的 virtual disk layer 上：

```text
physical disk/partition/file/memory
    -> offset/read-only/COW/sector conversion/synthetic MBR
    -> virtual disk
    -> internal disk, BIOS INT 13h, or EFI Block I/O export
```

map 使用事务模型：

```text
begin -> add mappings -> validate -> commit/install
```

这保留 GRUB4DOS `map --hook` 的延迟提交语义，并正确支持磁盘交换。

### 16.1 BIOS map

BIOS map 使用 blocklist 到 INT 13h 的转换。下一级启动后只保留：

- INT 13h/15h handler；
- mapping table；
- blocklist extents；
- bounce buffer；
- memdisk；
- BMIT/anchor。

不能降级为物理 blocklist 的来源必须转换为 memdisk、明确保留完整后端，或在 prepare 阶段拒绝导出。
高地址 memdisk 的分配、驻留搬运和 E820 保留必须遵循 §7.1，不能把 BIOS map 默认限制在低 4 GiB。

### 16.2 UEFI map

UEFI map 安装标准 Block I/O 和 Device Path handles。其 callback 和普通映射对象只需存在到 EBS，随后由固件/OS的正常 EBS 生命周期处理。

若 memdisk 或映射描述需要跨 EBS 由 OS 消费，其数据必须放入适当的 Resident/runtime-compatible memory，并通过 BMIT 描述。

必须检测 virtual disk provider lineage 中的映射环和对自身导出 handle 的递归访问。

## 17. Boot Mapping Information Table

BMIT 向下一级 bootloader 或 OS 描述本项目建立的 map。

发现机制：

| Firmware | Discovery |
| --- | --- |
| UEFI | 在 SMBIOS 中插入带项目 GUID 的 OEM Type `0xFE` anchor |
| BIOS | 低内存 cbtable-like anchor 指向高内存 BMIT |

SMBIOS OEM structure 只保存版本、项目 GUID、BMIT 地址、长度、CRC 和 boot session UUID。完整 blocklist 和字符串放在 BMIT 中。

BMIT 使用自包含、little-endian、offset-based wire format，至少描述：

- boot session UUID 和 generation；
- security/taint state；
- map source kind；
- image format；
- presentation type；
- read-only、memdisk、synthetic MBR 等 flags；
- logical/physical block size；
- logical LBA 到 source disk physical LBA 的 extents；
- GPT disk/partition GUID、MBR signature、filesystem UUID、device path 等 identities；
- 原始 UTF-8 文件路径；
- memdisk physical region；
- provider/driver identity 和 hash；
- image/manifest digest；
- parent/superseded BMIT 信息。

路径和 disk number 仅作为提示；物理 blocklist、稳定 identity 和 memdisk region 才能作为重建依据。

BMIT 只能在所有 map 冻结后生成。CRC用于损坏检测；以后 lockdown 模式应测量或认证 BMIT digest。

## 18. Supported loaders

只保留以下 loader families：

- EFI application chainload；
- BIOS boot sector；
- NTLDR；
- DOS；
- FreeLdr；
- Linux kernel；
- Multiboot 1/2；
- NT6 `bootmgr.exe`/wimboot；
- NT5 `osloader.exe`/相关 wimboot 路径。

BSD、XNU 等专用 loader 删除；它们仍可在协议允许时通过 EFI chainload、boot sector 或 Multiboot 启动。

每个 loader 必须实现 prepare/execute 两阶段接口，并生成统一 `boot_plan`。prepare 失败可以回到菜单；execute 成功路径不得返回。

EFI application、UKI、Linux EFI stub 和 Windows EFI loader 必须使用固件 `LoadImage/StartImage`，不得自行实现通用 EFI PE loader。

## 19. BIOS stage2-first boot model

初期 BIOS 版本不实现自身 MBR/PBR。发行：

- Multiboot2 stage2；
- Linux x86 boot protocol stage2；
- 稳定后可增加同时包含两种 header 的 hybrid image。

两种入口转换为统一 `boot_context`，其中包含 memory map、framebuffer、command line、resource archive 和上级 modules。

未来原生 BIOS 启动链为：

```text
MBR/PBR -> minimal stage1 -> dedicated BIOS boot partition -> stage2
```

主要安装模式不得要求 MBR/PBR 解析复杂文件系统。file blocklist 可以作为兼容模式，但不是首选模式。

## 20. Secure Boot and lockdown

初期 Secure Boot 模式尚不提供完整端到端验证。若后续组件未验证，必须显示清晰警告并要求本地物理确认，默认拒绝，确认只对本次启动有效。

安全状态至少包括：

```text
DISABLED
USER_CONFIRMED_TAINTED
LOCKDOWN
```

Secure Boot 启用时，无论是否确认继续，都必须禁止：

- Lua FFI；
- 任意函数地址调用；
- 任意物理内存和 I/O 写入；
- MSR/PCI raw write；
- 未受策略约束的 BIOS interrupt execution。

未来 lockdown 应验证或认证：

- ELF modules；
- Lua configuration；
- resource manifest；
- BIOS drivers；
- target kernel/loader；
- BMIT digest。

EFI drivers 和 EFI target images 始终通过固件 `LoadImage()`进入当前 Secure Boot policy。不得在被固件拒绝后使用自定义 PE loader 绕过。

模块和资源最好由一份签名 manifest 统一认证，而不是重新建立模块依赖或复杂包管理系统。项目应预留 TPM TCG2 measurement 和 event log 支持。

## 21. Network

初期 core 不包含自有网络栈。

UEFI 下通过以下能力获取网络：

- `ipxe.efidrv`；
- 固件或加载的 EFI network stack drivers；
- SNP、PXE Base Code、LoadFile/LoadFile2、HTTP 等标准 protocol。

BIOS stage2 的网络资源优先由上级 iPXE/GRUB 作为 Multiboot module 或 Linux initrd 传入。以后若实现独立网络能力，应作为大型模块或专用 driver，而不是进入 core。

## 22. Error handling and logging

废除全局 `grub_errno`。API 返回明确 `boot_status_t`，并允许附加错误 context chain。

最低层日志后端至少包括：

- serial；
- EFI serial protocol；
- QEMU debugcon；
- text console；
- memory ring buffer。

fatal path 不依赖 Lua、LVGL、filesystem 或动态模块。panic 后不得恢复菜单，只能记录状态并 reboot/halt。memory log 可以通过 BMIT 提供给下一级。

## 23. Installation and update

runtime 与 installer 完全分离。host-side 工具负责：

- resource packing；
- signing；
- inspection；
- EFI installation；
- 未来 BIOS stage1 installation。

更新采用 A/B 文件和原子 selector，保留 previous image/resource archive。不得依赖原位覆盖唯一可启动映像。

## 24. Testing requirements

GRUB2 移植的 storage code 必须能够通过 host shim 在普通进程中运行，以支持单元测试和 fuzzing。

测试映像应在测试时生成，不在仓库中保存不透明大型 binary blobs。测试范围至少包括：

- disk/partition/filesystem；
- cryptodisk/diskfilter/LVM/RAID；
- malformed/truncated images；
- ELF module relocation和ABI；
- Lua configuration；
- map/blocklist/memdisk；
- BMIT encode/decode；
- EFI driver loading；
- loader prepare/commit/handoff；
- allocation failure和I/O fault injection。

QEMU 集成矩阵至少覆盖 SeaBIOS i386、OVMF IA32、OVMF X64、AArch64 UEFI 和 LoongArch64 UEFI。自动判定使用串口标记，不依赖截图；LVGL 另做 framebuffer snapshot test。
