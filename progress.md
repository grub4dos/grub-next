# 实施进度

更新：2026-09-08。Phase 0 基线已完成，BIOS 验收采用 GRUB2 的 Multiboot2 加载路径。
Phase 1 尚未开始。GRUB4DOS 直接加载的限制见下文，不把它列为已验证能力。

## Phase 0 交付

- 沿用已有 Git repository；加入 GPL-3.0-or-later LICENSE、CODE_ORIGINS.md、
  references.lock.json 及来源校验工具，采用可审计迁移历史路线，不复制整套参考 runtime。
- 确定 `boot_` / `BOOT_` 命名空间、C11/独立汇编、目录布局和格式；生成 AGENTS.md。
- CMake 分离 host 与 target；五个 Clang toolchain、两个 x86 GCC 兼容 toolchain；
  BIOS linker script 和 EFI 原生 PE 链接参数；GitHub Actions 自动构建/启动/上传证据。
- 新增明确返回值 `boot_status_t` 与有界 COM1/debugcon 日志；未知状态、空日志参数、
  UART 不就绪时的超时与 debugcon 保留路径均有 host 测试。
- i386 BIOS 最小 Multiboot2 header、GDT、栈和 eager FP 初始化；x64 EFI hello 与测试父映像。
- 加入不同源码路径的独立双构建和 SHA-256 比较、PE 属性检查、动态 QEMU fixture。

## 验收证据

| plan.md 验收项 | 结果与证据 |
| --- | --- |
| host 和全部 target 可配置 | 通过；host、i386-pc、i386-efi、x86_64-efi、arm64-efi、loongarch64-efi 均实际编译 |
| x64 EFI 经 OVMF LoadImage/StartImage | 通过；测试父映像先验证损坏 PE 被拒绝，再加载并启动 hello，串口记录两个成功标记 |
| i386 Multiboot2 stage2 | 通过 GRUB2/SeaBIOS 路径；grub-file 识别 header，串口输出 hello |
| 两次相同构建 hash 相同 | 通过；不同源码/构建绝对路径下全部八个产物逐字节相同，SOURCE_DATE_EPOCH=1704067200 |

主验证命令（Ubuntu/WSL）：

```sh
python3 tools/check_references.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

本次完整 Clang 运行的证据保留在 `build/phase0/`，GCC 兼容运行在 `build/gcc-smoke/`。
可持久审查的 hash / 工具版本摘要见 `docs/phase0-evidence.json`。本地运行了与 CI 相同的脚本；
没有声称已在远端 GitHub Actions 上运行。

串口关键结果：

```text
BOOT:PASS:fp-state
BOOT:HELLO:i386-pc:multiboot2
BOOT:PASS:invalid-pe-rejected
BOOT:PASS:LoadImage
BOOT:PASS:fp-state
BOOT:HELLO:x86_64-efi
BOOT:PASS:StartImage
```

Clang 与 GCC 两种工具链的 BIOS/EFI 产物都通过 QEMU 启动。Clang 还通过 Pentium III、
486、关闭 FXSR、关闭 SSE 四个拒绝场景，均在 debugcon 输出 `BOOT:FAIL:cpu-sse2-required`，
没有进入 hello。486 测试只证明该 CPU 模型被拒绝，不声称覆盖真实无 CPUID 硬件的全部行为。
最低层 FP 状态读取 x87 control word / MXCSR，并执行 double 运算验证。

## 浮点基线

用户提供的基线已加入 DESIGN.md §5.2：x64 SSE/SSE2、ARM64 FP/SIMD、LoongArch LP64D；
i386 `-msse2 -mfpmath=sse`。不启用可选的 `-mno-80387`，保留标准 i386 浮点返回 ABI。
BIOS 入口在 C 前检查 CPUID/FPU/FXSR/SSE/SSE2，设置 CR0/CR4、FNINIT/MXCSR；
x64 EFI 入口与返回测试父映像前重置 FP 状态。整个实现没有 lazy FP。
ARM64/LoongArch 的编译对象架构已检查，LoongArch ELF flags 为 0x43（double-float / OBJABI v1）。
这些架构真正的 FP 状态初始化、ARM64 FPCR/FPSR 和 LoongArch FCSR handoff 规范化，
必须随 Phase 1 / 后续 loader 实现；本阶段无这些平台的执行入口。

## 已知边界与下一阶段

- IA32/ARM64/LoongArch EFI 当前只有 runtime 静态库编译探针，不是 EFI 可启动映像，
  没有实际 firmware 执行证据；Phase 0 的验收仅要求这些 target 可配置。
- ref/grub4dos 的当前 loader 提供 Multiboot1 路径，未发现 Multiboot2 直接加载实现。
  因此本阶段不声称 GRUB4DOS 直接启动成功，也不为此偷偷加入另一套 legacy 入口。
  后续 Linux boot protocol stage2 可提供相应兼容路径。
- Lua 版本已按用户决定统一为 5.5.1，与 ref/lua-5.5.1 来源锁一致；移植仍在 Phase 6。
- hello 有意返回固件测试父映像；这不是最终产品的 handoff 策略。
- 还没有 allocator、统一 boot_context、memory map、EFI protocol console、Lua、模块、存储、map、BMIT、
  Linux protocol stage2、正式 loader 或安全启动闭环；这些按 plan.md 后续阶段实现。
- 本阶段测试未启用 Secure Boot，不代表 Secure Boot 或实际硬件认证。

下一步按 Phase 1 建立统一 boot_context、三类内存所有权、完整平台入口和 fatal/reset 路径。

## 后续设计更新

Lua 基线统一为 5.5.1。DESIGN.md §7.1 增加 BIOS map --mem / initrd 使用 4 GiB 以上物理内存的要求，
并在 Phase 1、7、8、9 加入实现项与边界验收。已核对 GRUB4DOS 的 PAE/long-mode 搬运，
ref/wimboot 的 initrd 高地址搬运与分页 callback，
以及实际路径 `ref/syslinux-6.04-pre1/memdisk` 的驻留管理和 32 位地址限制。
本次为文档一致性与设计更新，没有新增 runtime 高内存支持；Phase 0 测试结论不变。
