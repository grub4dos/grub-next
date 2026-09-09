# grub-next

依据 [DESIGN.md](DESIGN.md) 和 [plan.md](plan.md) 开发的新 bootloader。
当前交付 Phase 1 平台和内存核心：BIOS Multiboot2/Linux 入口、四种 EFI 映像、统一 context、
三类内存所有权及 BIOS PAE 高内存访问。启动后运行验收探针，故意 panic/reset；尚无菜单或 OS loader。
项目采用 GPL-3.0-or-later，来源见 [CODE_ORIGINS.md](CODE_ORIGINS.md)。

## 构建与运行

在 Ubuntu 24.04 / WSL Ubuntu 中安装依赖：

```sh
sudo apt-get update
sudo apt-get install -y clang clang-format lld ninja-build cmake python3 python3-venv \
  qemu-system-x86 qemu-system-arm ovmf ovmf-ia32 qemu-efi-aarch64 \
  grub-pc-bin grub-common xorriso mtools gcc g++ pkg-config \
  libglib2.0-dev libpixman-1-dev libfdt-dev
python3 tools/prepare_phase1.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase1.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
```

`prepare_phase1.py` 校验固定 SHA-256，下载测试固件并在 `build/qemu-9.2.2/` 构建
LoongArch QEMU **host 模拟器**。项目 runtime 始终只由 CMake 构建。
Ubuntu 24.04 的 QEMU 8.2 不能加载本次选用的 16 MiB LoongArch 固件；
脚本使用独立 QEMU 9.2.2，不替换系统 QEMU。首次运行需要网络和数分钟。

Phase 1 测试为全部 target 构建产物，从不同源码路径独立重建并比较二进制，
再生成 ISO/ESP，在 QEMU 中检查串口成功标记及 reset 后退出。Linux 实模式路径另检查 debugcon。
完整证据保留在 `build/phase1/*.log` 和 `results.json`。
`--skip-build`、`--no-repro`、`--only bios` 等选项仅用于定向排查。
`QEMU_LOONGARCH`、`LOONGARCH_EFI` 可指定已有模拟器与固件。

单目标构建：

```sh
cmake -S . -B build/bios -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/i386-pc.cmake
cmake --build build/bios
cmake -S . -B build/efi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-efi.cmake
cmake --build build/efi
```

## 产物

| 配置 | Phase 1 产物 | 验证入口 |
| --- | --- | --- |
| host | `boot-core-test` | 内存、context、EFI 描述符、分块访问及日志测试 |
| i386-pc | `boot-core.elf` | GRUB2 Multiboot2 |
| i386-pc | `boot-linux.bz` | GRUB2 Linux 32 位入口、SeaBIOS Linux 实模式 setup |
| i386-efi | `boot-core.efi` | OVMF IA32，`BOOTIA32.EFI` |
| x86_64-efi | `boot-core.efi` | OVMF X64，`BOOTX64.EFI` |
| arm64-efi | `boot-core.efi` | AAVMF，`BOOTAA64.EFI` |
| loongarch64-efi | `boot-core.efi` | EDK2 LoongArch，`BOOTLOONGARCH64.EFI` |

保留 Phase 0 的 `boot-stage2.elf`、`boot-hello.efi`、`boot-efi-test.efi` 和架构静态库探针，
以持续检查已有基线。它们不代表 Phase 1 的产品入口。
所有 EFI 映像均由固件装载；LoongArch 使用构建期 ELF→PE 转换，仅接受相对重定位并生成 PE DIR64，
runtime 没有自制 PE loader。

## 内存接口与验证边界

[平台/内存接口说明](docs/phase1.md) 包含调用约束、来源及验收证据。

- BL 支持显式释放；Loader 支持 begin/abort/commit；Resident 不释放。
- BIOS 使用 64 位物理地址、2 MiB PAE 窗口和低于 512 KiB 的 4 KiB bounce buffer。
  QEMU 6 GiB、有 PAE 无 long mode 场景验证高地址分块搬运、数据哈希及 CR0/CR3/CR4、FP 状态恢复。
- 跨 4 GiB 的**连续可用区域**由 host 夹具验证共享分块实现；普通 QEMU PC 在 4 GiB 下方存在
  固件/PCI 保留区，跨越该区域的申请必须拒绝。没有把越过保留区的写入当作支持证据。
- E820 Resident handler 使用低内存内自包含表，安装后冻结内存分配；测试通过真实 INT 15h 回读。
- EFI 每次分配刷新内存图，通过固件 AllocatePages/FreePages 管理实际内存。
  ARM64/LoongArch 的 Resident 使用 64 KiB 粒度，并回读固件图验证保留类型。
- 本阶段未验证真实硬件、Secure Boot、GRUB4DOS 直接启动、INT 13h map 或 OS handoff。

## Phase 2 模块 SDK

模块 API、metadata 格式、树外构建命令和生命周期约束见 [Phase 2 SDK](docs/phase2.md)。
运行 `SOURCE_DATE_EPOCH=1704067200 python3 tests/phase2.py` 可验证五个 target 的实际模块
执行、失败回滚、重复加载拒绝及 SDK 独立构建。日志保存在 `build/phase2/`。
当前样本为编译期嵌入；resource archive 与签名认证分别留在后续阶段。

## 额外检查

```sh
sudo apt-get install -y gcc-mingw-w64-x86-64
python3 tests/gcc_smoke.py
cmake -S . -B build/sanitizers -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DBOOT_SANITIZERS=ON
cmake --build build/sanitizers
ctest --test-dir build/sanitizers --output-on-failure
python3 tools/check_references.py
git diff --check
```

GCC 兼容测试覆盖 x86 BIOS、Linux setup、X64 EFI 的 Phase 0/1 启动。
host 核心测试可启用 ASan/UBSan。
新代码使用 C11、UTF-8/LF、四空格；C 花括号采用 Allman 换行，规则保存在 `.clang-format`。
