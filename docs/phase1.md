# Phase 1 平台与内存接口

Phase 1 提供可启动的平台/内存验收切片。正式 loader、OS handoff、map 和 BMIT
仍由后续阶段实现。测试核心运行结束故意 panic/reset，不返回菜单或固件父应用。

## 初始化与分配

每个入口构造一个 `struct boot_context`，包含入口类型、固件信息、内存图、资源地址与平台函数表。
BIOS 接受 Multiboot2 memory-map/modules，或 Linux boot_params 的 E820/initrd；
Linux `setup_data` 链目前明确返回 unsupported，不忽略未知资源链。
32 位 Linux 入口不凭空拼接高位 initrd 字段。
EFI 保存 image handle、system table 和固件 revision，识别 BS、RT、Loader 等内存类型。

core 不依赖 host libc，错误通过 `boot_status_t` 返回；没有全局 errno。

```c
/* context 已完成平台初始化，maximum 是不包含在内的物理地址上界。 */
uint64_t address;
boot_status_t status = boot_loader_begin(&context->memory);
if (status == BOOT_OK)
{
    status = boot_memory_alloc(&context->memory, BOOT_LOADER, 8192, 4096,
                               0x100000, 0x80000000, &address);
    if (status != BOOT_OK)
        boot_loader_abort(&context->memory);
    else
        boot_loader_commit(&context->memory);
}
```

`boot_memory_alloc` 从最高可用且符合约束的地址分配，检查长度、对齐、上界、所有权和地址溢出。
输入保留用 `boot_memory_reserve` 记录，不能把该接口当成固件 AllocatePages。
BL 通过 `boot_memory_release` 释放；Loader abort 只释放当前事务，commit 保留所有权记录，
后续 abort 不能释放已提交对象；Resident 不释放。
最多 256 个原始区域、128 条分配记录，容量不足显式返回错误。

EFI 分配前刷新固件图，并对请求地址执行 AllocatePages：
BL/Loader 使用 EfiLoaderData，Resident 使用 EfiReservedMemoryType。
固件可能拒绝图中标为 conventional 的特殊预留范围；分配器有界尝试较低地址，
最多 256 次固件拒绝，避免把固件内部约束当作整机 OOM。
FreePages 失败时保留对应记录并返回 I/O error；事务仍可检查/重试。

## BIOS 物理访问

`boot_phys_read/write` 使用已保留的 RAM 区间；`boot_phys_copy` 支持 64 位长度和重叠搬运，
通过 4 KiB bounce buffer 分块。可直接给出高于 4 GiB 的物理地址，不能将它转换成普通 C 指针。
2 MiB 分块逻辑位于 `core/physical.c`，由 host 与 BIOS 共用。

PAE 检测独立于 SSE2；无 long mode 不影响 PAE 路径。
支持的调用状态是单 CPU、平坦保护模式且分页关闭；不接受未知的调用者页表。
每块访问在中断关闭时短暂打开 PAE，完成后恢复 CR0/CR3/CR4 和调用者 flags。
代码、栈、页表和传入的普通 buffer 必须位于 2 GiB 窗口下方；窗口地址不能跨调用保存。
页表由 core 保留，bounce buffer 通过分配器取得，位于 64 KiB～512 KiB 之间且不跨 64 KiB 边界。

`boot_memory_access` 同时检查原始 RAM 图、CPU 地址位宽和完整所有权范围。
固件/PCI 孔洞不会因创建一条保留记录而变为可访问 RAM。
没有 PAE 时采用低地址路径；高地址不足、低 bounce 不足或溢出均返回错误，不截断地址。

`boot_bios_resident_install` 导出当前 E820 图，将 handler、原 INT 15h vector、64 位描述表和
验证 trampoline 放入同一低内存 Resident 区域。handler 用 CS 相对寻址，其他 INT 15h 调用链至原固件。
安装后冻结分配；后续 loader/map 阶段应在最终 commit 时调用，不能在菜单初始化时提前冻结。

## 平台输出与停止

- BIOS：有界 COM1、debugcon、VGA 文本输出，RTC 日期/时间，reset/halt。
- EFI：Simple Text Output 与配置有限 timeout 的 Serial I/O、GetTime、ResetSystem。
- 所有输出同时进入 4 KiB memory log ring，panic 不依赖 Lua、LVGL、文件系统或模块。
- X86 规范化 FNINIT/MXCSR；ARM64 规范化 FPCR/FPSR；LoongArch 汇编入口先启用 EUEN.FPE，
  再进入 LP64D C，并规范化 FCSR。没有 lazy FP。

最低层 console 当前用于 ASCII 诊断。通用 UTF-8 菜单/字体属于后续 UI 阶段。
BIOS 实模式 Linux setup 要求至少 516 KiB conventional memory，使用 0x80000 的 boot_params 副本；
与副本重叠的 setup 装载位置明确拒绝。GRUB2 32 位入口直接消费上级参数，不运行该 setup。

## 验收证据与范围

| 证据 | 结论 | 实现/证据路径 |
| --- | --- | --- |
| host 边界、8000 次分配序列、ASan/UBSan | 对齐/重叠/事务、损坏输入及溢出拒绝 | tests/core_test.c、build/sanitizers.log |
| host 0xfffff000 开始的连续窗口数据回读 | 共享分块代码跨 4 GiB 无截断；完整字节相等 | core/physical.c、tests/core_test.c |
| QEMU 6 GiB / qemu32,+pae,-lm | 高地址重叠 copy、跨 2 MiB 窗口、FNV-1a 哈希与 CPU/FP 状态一致 | build/phase1/bios-pae-no-lm.serial.log |
| 无 PAE、低 RAM、无可用低地址、溢出 | 低地址回退与明确拒绝 | BIOS 串口日志及 host 测试 |
| 实际 INT 15h 回读全部 24 字节描述符 | Resident 区域包括高地址区域均保留 | platform/bios/resident.S、BIOS 日志 |
| GRUB2 MB2、GRUB2 Linux、SeaBIOS Linux setup | 同一 C runtime/context；实模式额外检查 debugcon | build/phase1/bios-*.log |
| 四种 EFI 固件启动与 PE relocation probe | 实际可运行入口，非配置探针 | build/phase1/*efi.serial.log |
| EFI GetMemoryMap 回读分配对象 | BS/RT/Loader 分类、Resident 固件保留类型正确 | tests/efi_memory_probe.c |
| 不同源码路径独立重建比较 | 七个 Phase 1 产物逐字节相同 | build/phase1/results.json |

普通 PC 的 4 GiB 下方包含保留区，不能取得跨越该边界的连续 E820 可用 RAM。
因此跨边界的连续读写证据来自 host aperture；QEMU 证明真实高地址访问及保留区拒绝，
**不声称在 QEMU PC 上写入了连续横跨 4 GiB 的普通 RAM**。
超过 4 GiB 的长度在物理区域/分配器测试中验证；本阶段没有搬运完整 4 GiB 文件的性能或耗时结论。
Phase 7/9 仍须按目标协议验证 initrd 和跨 handoff 的 memdisk，并建立自包含 Resident PAE 后端。

本次固件/模拟器边界：SeaBIOS 与 OVMF/AAVMF 来自 Ubuntu 24.04 包，
LoongArch 使用 [QEMU v9.2.2 的 EDK2 固件](https://github.com/qemu/qemu/tree/v9.2.2/pc-bios)
与独立 QEMU 9.2.2。下载哈希固定在 tools/prepare_phase1.py。
ARM64/LoongArch 64 KiB Resident 粒度与
[EDK2 分配规则](https://github.com/tianocore/edk2/blob/edk2-stable202408/MdeModulePkg/Core/Dxe/Mem/Page.c)
一致。固件哈希及实际命令写入结果文件。

未覆盖真实硬件、Secure Boot、GRUB4DOS 直接启动或 OS handoff。
CI 已加入同一脚本，但本次只报告本地运行结果，不声称远端 Actions 已执行。
