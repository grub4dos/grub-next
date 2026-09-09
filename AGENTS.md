# grub-next 开发约定

## 设计与范围

先读 DESIGN.md、plan.md 和 progress.md。DESIGN.md 是架构基线，plan.md 定义阶段验收；
用户明确调整优先。按阶段交付可运行纵向切片，不把后续阶段的配置探针报告成完整平台支持。
现有仓库名 grub-next 暂作工程标识；公开 C 命名空间使用 `boot_` / `BOOT_`，头文件放在 `include/boot/`。

## 目录与构建

- `core/`：平台无关基础代码；不得依赖 host libc。
- `platform/bios/`、`platform/efi/`、`platform/x86/`：入口和平台实现。
- `host/`、`tools/`：host 工具；`tests/`：动态生成 fixture 的自动验证。
- `cmake/toolchains/`、`linker/`：交叉工具链和映像布局。
- `ref/`：只读参考，默认不参与构建；`references.lock.json` 锁定来源。
- `build/`：生成物与测试证据，不提交。

CMake 是唯一构建描述；脚本只能编排 CMake、生成数据和测试，不能私自编译另一套 runtime。
新代码用 C11、独立 `.S` 汇编、UTF-8、LF、四空格，遵守 `.clang-format`；C 花括号采用 Allman 换行格式。
保留既有版权；新代码加 SPDX `GPL-3.0-or-later`。迁移文件必须保留上游版权并更新 CODE_ORIGINS.md。
不得改动 ref/ 来修复新工程构建；迁移前运行 `python3 tools/check_references.py`，
记录源文件、提交和适配差异。不要静默更新来源锁。

## Runtime 约束

只支持设计列出的五个 target；单 CPU、单线程、轮询，不使用 lazy FP。
Lua 固定为 5.5.1，与 `ref/lua-5.5.1/` 和来源锁一致。
BIOS 高物理内存遵循 DESIGN.md §7.1：64 位地址/长度、PAE 窗口、低地址 bounce buffer；
map --mem 支持高地址驻留，initrd 最终位置须遵守目标协议，不能因 core 为 i386 而一律限制在低 4 GiB。
API 显式返回 `boot_status_t`，不恢复全局 `grub_errno`。
最低层日志和错误路径不依赖 Lua、LVGL、文件系统或模块。串口轮询必须有界。
BIOS 在运行 C/SSE 指令前检查 CPUID/FPU/FXSR/SSE/SSE2 并设置 CR0/CR4。
x64 使用 x86-64 baseline SSE2；ARM64 使用 FP/SIMD；LoongArch64 使用 LP64D。
实现各架构 handoff 时规范化 x86 FNINIT/MXCSR、ARM64 FPCR/FPSR、LoongArch FCSR。
不引入模块依赖或卸载、通用硬件驱动栈、旧 GRUB parser、通用文件系统写入。
EFI 映像交给固件 LoadImage/StartImage；不得用自制 PE loader 绕过固件。
Phase 0 hello 返回固件仅是测试探针，不能作为产品 loader 的返回策略。

## 验证和交付

在 Ubuntu/WSL 内运行 `python3 tests/phase0.py`；依赖和单目标命令见 README.md。
编译成功不等于启动成功；自动启动以串口标记判定，CPU 拒绝路径用 debugcon。
保留 `build/phase0/*.log` 和 `results.json`；可复现性必须比较不同源码路径的独立构建。
按变更范围运行必要验证，最后运行 `git diff --check`，更新 progress.md 的事实、命令和边界。
不覆盖无关工作树改动，不未经请求提交或推送。
