# New Bootloader Implementation Plan

## 1. Planning principles

实施采用可启动的纵向切片。每个阶段都必须产生可测试结果，不同时重写所有子系统。

优先级如下：

1. 建立可运行 stage2 和 EFI image。
2. 固定 core API、内存和模块 ABI。
3. 移植存储栈并使其可在 host 上测试。
4. 用 Lua 取代 GRUB parser。
5. 实现 loader 和 map。
6. 增加 LVGL、驱动扩展和安全能力。
7. 最后补充 BIOS 原生 MBR/PBR 启动链。

每个 milestone 的完成条件包括：代码、自动测试、错误路径和最小文档，不以“能够编译”作为完成标准。

## 2. Phase 0: Repository and baseline

### Work

- 创建新 repository、GPL-3.0-or-later LICENSE 和 `CODE_ORIGINS.md`。
- 导入需要的 GRUB2/GRUB4DOS/NTloader/wimboot 源码快照或建立可审计的迁移历史。
- 确定代码命名空间、目录结构和格式规则。
- 配置 CMake、toolchain files、linker scripts 和基础 CI。
- 建立 serial/debugcon 日志和统一 `boot_status_t`。
- 建立 reproducible-build 检查。

### Acceptance

- host tools 和空 target runtime 可在所有 target 配置阶段通过。
- x86_64 EFI hello image 可由 OVMF `LoadImage/StartImage`运行。
- i386 Multiboot2 hello stage2 可由 GRUB2/GRUB4DOS 加载。
- 两次相同构建生成相同 hash。

## 3. Phase 1: Platform core and memory

### Work

- 实现 platform API：console、time、memory map、reset、halt、firmware information。
- 实现 BL、Loader、Resident 三类 allocator。
- 实现统一 `boot_context`。
- 实现 Multiboot2 i386 entry。
- 实现 Linux x86 boot protocol entry。
- 实现 EFI entry：IA32、X64、ARM64、LoongArch64。
- 实现最低层 panic path 和 memory log ring。

### Acceptance

- 所有入口转换成同一 `boot_context`。
- Loader allocation 可以整体 abort/commit。
- Resident allocation 在 BIOS E820 中正确保留。
- UEFI 可以正确识别 BS/RT/Loader memory types。
- 故意触发 panic 时，Lua/LVGL 尚未存在也能输出并 reset/halt。

## 4. Phase 2: ELF module ABI and SDK

### Work

- 实现 ELF ET_DYN loader。
- 只支持白名单 relocation。
- 实现 module ELF note、UUID、ABI 和 capability validation。
- 实现 `boot_api` table。
- 实现事务式 module registration。
- 禁止 module dependency和unload。
- 创建 external module SDK 和 sample modules。
- 建立 malformed ELF 和 ABI mismatch fuzz/unit tests。

### Acceptance

- 源码树外 sample module 可独立构建并在所有适用 target 加载。
- 模块不存在普通 undefined imports。
- 重复加载、错误架构、错误 ABI、非法 relocation 和截断 ELF 被可靠拒绝。
- module init 失败不会留下部分注册项。

## 5. Phase 3: Resource archive

### Work

- 实现只读 `cpio newc` reader。
- 生成每个 target 的 `resource.cpio`。
- 支持 EFI embedded section、Multiboot2 module 和 Linux initrd 三种输入。
- 实现 deterministic packer 和 content manifest。
- 将 sample modules、默认 `boot.lua` 和最小字体放入 archive。

### Acceptance

- BIOS stage2 在没有可识别磁盘文件系统时也能加载内嵌模块和配置。
- EFI image 可加载相同逻辑资源。
- archive 内容顺序、metadata 和 hash 可复现。
- 损坏 archive 不导致越界访问。

## 6. Phase 4: Storage core extraction

### Work

- 定义新的 block、partition、filter 和 filesystem API。
- 移植 GRUB2 disk core，但删除 nativedisk、ATA/AHCI 和 GRUB USB controller drivers。
- 实现 BIOS INT 13h provider。
- 实现 EFI Block I/O provider。
- 移植 GPT/MBR 和目标 partition parsers。
- 移植首批文件系统：FAT、ISO9660、NTFS、ext2/3/4。
- 建立 host file-backed block shim。
- 实现 provider lineage、stable identity 和 generation。
- 实现 logical/physical block size 和 alignment handling。
- 实现固定上限 read cache 和 invalidation。

### Acceptance

- 相同 filesystem code 可在 host test 和真实 boot target 中运行。
- 512、2048和4096字节逻辑块设备测试通过。
- firmware rescan 后不会复用 stale handle/cache。
- 所有目标文件系统均可读取大型文件、稀疏布局和边界条件样本。
- 自动测试不依赖仓库内大型 binary fixture。

## 7. Phase 5: Diskfilter and cryptodisk

### Work

- 移植 loopback、cryptodisk 和 diskfilter。
- 移植 LVM 和目标 RAID formats。
- 将公共能力放入 core API 或静态链接进单个模块，不引入模块依赖。
- 明确 physical blocklist 可追溯性。
- 为嵌套 provider lineage 和 map-loop detection 建立测试。

### Acceptance

- LVM/RAID/cryptodisk 可以从 BIOS INT 13h 和 EFI Block I/O provider 上工作。
- 无法降级成 physical blocklist 的文件来源会被正确识别。
- 模块加载顺序不依赖 `moddep.lst`。

## 8. Phase 6: Lua runtime and configuration

### Work

- 移植固定版本 Lua 5.4。
- 实现受限 allocator、instruction limit 和受限标准库。
- 定义 disk/fs/module/menu/boot Lua APIs。
- 实现 Lua source loader，不支持 bytecode。
- 实现默认配置搜索顺序和 embedded fallback。
- 实现文本 Lua REPL 和配置错误恢复。
- 删除 GRUB parser、normal mode、旧 menu parser 和 `grub.cfg` 支持。

### Acceptance

- 完整菜单可由 `boot.lua` 构造。
- 语法错误、OOM、无限循环和 API error 能返回明确错误界面。
- 项目产物中不存在旧 GRUB parser 的运行时路径。
- 同一 Lua menu model 可被 headless/text frontend 使用。

## 9. Phase 7: Initial loaders

### Work

- 定义统一 `boot_plan` 和 loader prepare/execute API。
- 实现 BIOS Linux x86 loader。
- 实现 Multiboot 1/2 loader。
- 实现 EFI application chainloader。
- 实现 EFI Linux stub/UKI chainload。
- 实现 BIOS boot sector chainloader。
- 实现 loader abort/commit 和 no-return fatal policy。

### Acceptance

- prepare 阶段所有错误均可返回菜单。
- commit 后任何 loader return 都进入 fatal/reset。
- Linux、Multiboot2 和 EFI application 可在对应 QEMU target 自动启动并输出成功标记。
- EFI target images 全部通过固件 `LoadImage()`加载。

## 10. Phase 8: Windows and legacy loaders

### Work

- 移植 NTLDR、DOS 和 FreeLdr chainload。
- 集成 NT6 `bootmgr.exe`/wimboot 路径。
- 集成 NT5 `osloader.exe`相关路径。
- 对外提供统一 Windows boot request，内部按 BIOS/EFI 和 NT 版本分流。
- 增加 WIM、VHD/VHDX 和文件注入相关测试。

### Acceptance

- BIOS 下可自动测试 FreeDOS、FreeLdr/ReactOS 和目标 NT loader 路径。
- EFI 下 Windows 路径使用 `bootmgfw.efi`或其他标准 EFI target。
- loader 不依赖 BSD/XNU 等已删除代码。

## 11. Phase 9: Virtual disk and BIOS map

### Work

- 建立 platform-independent virtual disk object。
- 实现 offset、read-only、memdisk、sector translation 和 synthetic MBR filters。
- 实现 map transaction。
- 移植 GRUB4DOS blocklist解析和 INT 13h map handler。
- 实现 drive swapping、floppy/HDD/CD presentation。
- 实现 Resident handler/blocklist/memdisk layout。
- 实现统一 INT 15h E820 reservation。

### Acceptance

- mapped floppy/HDD/CD images 可由 BIOS child读取。
- 磁盘交换按事务一次性提交，不受命令执行顺序破坏。
- fragmented physical blocklist 和 memdisk 测试通过。
- cryptodisk/LVM 等不可物理化来源会转 memdisk 或明确拒绝。
- chainload 后 BL memory 可被覆盖而 resident map 仍工作。

## 12. Phase 10: UEFI Block I/O map

### Work

- 移植现有 GRUB4DOS EFI Block I/O map。
- 安装 Device Path 和必要的 child handles。
- 支持 HDD/CD/removable presentation。
- 实现 handle/media identity 和 map-loop detection。
- 实现映射 commit 后 protocol rescan。
- 验证 EBS 后不依赖任何 Block I/O callback。

### Acceptance

- chainloaded EFI loader 在 EBS 前可读取 mapped image。
- callback 不会递归进入自身导出 handle。
- child 返回时按 no-return policy reset，不尝试恢复 map/UI。
- EBS 后只保留 BMIT 指定的 Resident information/memdisk。

## 13. Phase 11: BMIT handoff protocol

### Work

- 定义版本化 BMIT wire format 和公开头文件。
- 实现 disk/map/extent/path/memory/provider records。
- 实现 CRC、bounds checking、generation 和 parent chaining。
- UEFI 下实现 SMBIOS OEM Type `0xFE` anchor。
- BIOS 下实现低内存 anchor 和高内存 resident table。
- 提供 host-side BMIT inspect tool 和 sample consumer。

### Acceptance

- UEFI 和 BIOS 生成完全相同的 BMIT body。
- consumer 能恢复 map image type、blocklist、disk identity、path 和 memdisk region。
- BMIT 中不存在指向 BL/Loader memory 的指针。
- 多层相同 bootloader chain 可识别最新 generation 和 parent table。
- malformed BMIT fuzz tests 不产生越界访问。

## 14. Phase 12: EFI and BIOS extension drivers

### Work

- 实现 EFI driver `LoadImage/StartImage/ConnectController/rescan`流程。
- 根据 LoadedImage memory type 区分 BS/RT driver，不管理其生命周期。
- 集成 `ipxe.efidrv`测试。
- 测试 standalone `NvmExpressDxe`等标准协议驱动。
- 定义 BIOS resident driver最小结果描述。
- 初步集成一个 BIOS USB 或 NVMe INT 13h driver。
- 确保 driver hook 先于 map hook。

### Acceptance

- EFI driver 只通过固件 Image Services 加载校验。
- BS driver 在 EBS 前提供 protocol；RT driver 内存由固件/runtime管理。
- `ipxe.efidrv`加载后可以发现新增 NIC protocol。
- EFI storage driver 加载后可以发现新增 Block I/O handles。
- BIOS driver 新增 drive 后，map 可以建立在其 INT 13h provider 之上。

## 15. Phase 13: LVGL frontend

### Work

- 移植裁剪后的 LVGL。
- 实现 GOP、VBE、keyboard 和可选 pointer backend。
- 实现 text/LVGL 共用 menu model。
- 实现统一 XRGB8888/ARGB8888 framebuffer 和像素转换。
- 加载外部字体、图标和主题资源。
- 实现 GUI failure fallback。

### Acceptance

- 同一 `boot.lua`可在 text 和 LVGL frontend运行。
- GOP/VBE 常见 pixel formats 和 pitch 测试通过。
- 无 GOP、资源损坏或 LVGL init失败时自动进入 text frontend。
- framebuffer snapshot tests 稳定可复现。

## 16. Phase 14: libffi and debugging APIs

### Work

- 为目标架构移植 libffi outbound call。
- 实现受限 type/signature model，不引入 C declaration parser。
- 实现 memory、I/O、MSR、PCI、BIOS interrupt 和 EFI protocol调试 API。
- 使用 userdata 表示地址和指针。
- 在 C security policy layer 禁止 Secure Boot 下的 FFI/raw writes。

### Acceptance

- BIOS i386 可从 Lua 调用测试 C ABI 函数和 BIOS interrupt wrapper。
- 64 位 target 地址不会经过 Lua double。
- Secure Boot enabled 时不存在通过 Lua、模块或间接 API 启用 FFI 的路径。
- 不支持 executable closure/trampoline。

## 17. Phase 15: Secure Boot warning mode

### Work

- 检测 SecureBoot、SetupMode 和 firmware image policy。
- 实现本地物理确认和 session taint state。
- 显示组件路径和 SHA-256。
- 禁止自动或远程输入确认。
- 将 security state 写入 BMIT。
- 为 resource manifest 和未来签名预留格式。

### Acceptance

- Secure Boot 下未验证组件默认拒绝。
- 用户确认只在当前 session有效。
- FFI/raw debug始终禁止，不受确认影响。
- 被固件 `LoadImage()`拒绝的 EFI image不能通过替代路径绕过。

## 18. Phase 16: Network consumption

### Work

- 消费 EFI SNP、PXE Base Code、LoadFile/LoadFile2 和可用 HTTP protocols。
- 支持从上级 loader传入网络下载的 resource archive。
- 不在 core 中实现自有 TCP/IP stack。
- 将独立网络栈需求留给大型模块或 iPXE。

### Acceptance

- UEFI 环境可通过标准 protocol取得配置、module或target image。
- BIOS stage2 可消费 iPXE/GRUB传入的网络资源。
- core size和依赖图中不出现完整网络栈。

## 19. Phase 17: Installer and A/B update

### Work

- 实现 host-side pack、inspect、sign 和 install tools。
- 实现 EFI removable/vendor path installation。
- 实现 current/previous A/B files和原子 selector。
- 增加失败回滚和版本检查。

### Acceptance

- 中断更新不会破坏唯一可启动版本。
- 可以显式选择 previous version。
- runtime 不包含安装器逻辑。

## 20. Phase 18: Native BIOS stage1

### Work

- 设计 minimal MBR/PBR/stage1。
- 优先支持 dedicated BIOS boot partition。
- 从固定布局加载完整 stage2/resource archive。
- 提供 file blocklist 兼容安装模式。
- 与 Multiboot2/Linux-protocol stage2共享完全相同的 common core。

### Acceptance

- dedicated boot partition方案不依赖解析复杂文件系统。
- stage1 更新失败可由 host installer恢复。
- 原生 BIOS、Multiboot2 和 Linux protocol入口运行相同 stage2测试套件。

## 21. Phase 19: Lockdown and measured boot

### Work

- 实现签名 resource manifest。
- 验证 ELF module、Lua config、BIOS driver和目标映像。
- 集成 TPM TCG2 measurement和event log。
- 测量 BMIT digest和boot plan关键输入。
- 实现完整 lockdown capability policy。
- 增加 anti-rollback和key enrollment设计。

### Acceptance

- Secure Boot lockdown下不存在未验证 native code执行路径。
- 配置、模块、driver和target的验证结果可由日志/BMIT审计。
- 修改任一受保护组件都会导致拒绝或明确进入非-lockdown模式。
- FFI、raw memory/I/O write和未验证chainload不可用。

## 22. Continuous test matrix

每个 phase 都应持续运行：

| Layer | Tests |
| --- | --- |
| Host unit | allocator、ELF、CPIO、BMIT、filesystem、filter、Lua API |
| Fuzz | malformed disk/FS/ELF/archive/BMIT/config |
| BIOS integration | SeaBIOS、INT 13h、stage2、map、legacy loader |
| EFI integration | OVMF IA32/X64、driver load、Block I/O map、EBS |
| Other architectures | AArch64 UEFI、LoongArch64 UEFI |
| UI | text snapshots、framebuffer snapshots、fallback |
| Security | Secure Boot policy、taint、forbidden FFI/raw access |
| Reproducibility | binary hash comparison |

测试 fixture 应由公开脚本和公开格式工具动态生成。无法动态生成的最小样本必须附带来源、生成方法、hash 和人工审计说明。

## 23. First usable release

首个可用版本建议在 Phase 11 后发布，包含：

- i386 Multiboot2/Linux-protocol stage2；
- IA32/X64/ARM64/LoongArch64 EFI images；
- ELF module SDK；
- resource archive；
- Lua text menu；
- GRUB2-derived storage stack；
- Linux、Multiboot、EFI和Windows核心 loader；
- BIOS/UEFI map；
- BMIT。

LVGL、libffi、外部硬件 driver、完整网络、原生 BIOS stage1和lockdown可以在后续版本交付，不阻塞首个架构闭环。
