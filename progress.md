# 实施进度

更新：2026-09-09。Phase 1 平台/内存切片已实现，全部平台入口有实际 QEMU 启动证据。
连续跨 4 GiB RAM 的测试范围见下文；未将普通 PC 的保留区当作可写 RAM。

## Phase 1 交付

- 共用 `boot_context` 和 platform API：console、固件日期/时间、memory map、
  固件信息、reset/halt；4 KiB memory log ring 和独立 panic。
- BIOS Multiboot2、Linux 32 位入口和 Linux 实模式 setup 使用同一个 CMake runtime payload。
  保留 command line、framebuffer、resource 和上级 module 元数据，并保留对应输入内存。
- IA32、X64、ARM64、LoongArch64 EFI 均生成可启动 PE 映像，由固件加载。
  LoongArch 汇编入口在 C 前启用 EUEN.FPE；各架构规范化 FP 控制状态。
- BL 分配/释放、Loader begin/abort/commit、Resident 保留与冻结；
  固件类型由 EFI AllocatePages/FreePages 实际落实，不仅修改本地记账表。
- BIOS 64 位物理区域、独立 PAE/地址位宽检测、2 MiB 临时映射窗口、
  低于 512 KiB 的 4 KiB bounce、重叠分块 copy 和输入范围校验。
- CS 相对的 Resident INT 15h E820 handler，安装后冻结图；
  通过真实模式 INT 15h 回读整个表，检查包括高地址 Resident 区域。
- ARM64/LoongArch Resident 按 64 KiB 粒度保留；EFI 分配前刷新固件图，
  对固件拒绝的特殊范围有界尝试较低地址。
- C 花括号统一为 Allman 换行；更新 `.clang-format` 和 AGENTS.md。
  `ref/`、来源锁没有改动，没有提交或推送。

## Phase 1 验收证据

| 验收项 | 结果与边界 |
| --- | --- |
| 全部入口统一 context | GRUB2 MB2、GRUB2 Linux、SeaBIOS Linux setup、四种 EFI 实际启动；Linux 实模式另有 debugcon 标记 |
| Loader 整体 abort/commit | host 和所有 runtime 通过；已提交对象不被后续事务 abort 释放 |
| Resident E820 保留 | BIOS 实际 INT 15h 回读匹配；包含 4 GiB 以上 Resident 区域 |
| QEMU 高地址访问 | 6 GiB RAM，qemu32,+pae,-lm；高地址跨 2 MiB 窗口及重叠 copy 的数据哈希、CR0/3/4、FP 状态一致 |
| 跨 4 GiB 连续区域 | host aperture 使用同一 core 分块实现，从 0xfffff000 跨界读写，完整字节比较通过；普通 QEMU PC 在边界下方存在保留区，不能声称实测连续可用 RAM 跨界写入 |
| 无 PAE、低内存不足、溢出 | 无 PAE QEMU 低地址回退通过；host/启动探针覆盖低地址不足和溢出拒绝 |
| EFI BS/RT/Loader 类型 | 所有 EFI 平台识别三类，并通过 GetMemoryMap 回读确认 LoaderData/Resident ReservedMemoryType |
| 独立 panic | 所有入口输出预期 panic 并 reset；QEMU 正常退出。无 Lua/LVGL/文件系统/模块依赖 |
| 可复现 | 不同源码路径独立双构建，七个 Phase 1 产物逐字节相同 |
| 兼容与诊断 | x86 GCC Phase 0/1 启动通过，host ASan/UBSan 通过，git diff --check 通过 |

本次完整运行命令（Ubuntu/WSL）：

```sh
python3 tools/check_references.py
python3 tools/prepare_phase1.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase1.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
cmake -S . -B build/sanitizers -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DBOOT_SANITIZERS=ON
cmake --build build/sanitizers
ctest --test-dir build/sanitizers --output-on-failure
git diff --check
```

Phase 1 九个 QEMU 场景、命令、映像/固件 SHA-256 保存在 `build/phase1/results.json`，
串口与模拟器日志在同目录；可持久审查的摘要见
[docs/phase1-evidence.json](docs/phase1-evidence.json)。
GCC 证据在 `build/gcc-smoke/`，sanitizer 结果在 `build/sanitizers.log`。
CI 已加入同一验证流程；这里仅声明本地执行结果，没有远端 Actions 运行声明。

## 本阶段边界

- 普通 QEMU PC 的跨 4 GiB 保留区必须拒绝；连续跨界数据验证是 host 夹具，
  真实高地址 RAM 访问则由 QEMU 证明。后续验收不得省略这个区别。
- 超过 4 GiB 长度目前验证区域管理和事务记账；没有声称搬运了完整 4 GiB 文件。
- Resident E820 安装后不再允许分配，后续应在最终 commit 使用；
  跨 OS handoff 的 Resident PAE/INT 13h map、BMIT 和 initrd 最终地址协议仍在 Phase 7/9/11。
- Linux setup_data 链明确拒绝；Linux 实模式 setup 的低内存/装载位置限制见 docs/phase1.md。
- 尚未验证 GRUB4DOS 直接启动、真实硬件或 Secure Boot。
- Phase 1 `boot-core` 是运行验收后 panic/reset 的纵向切片，没有菜单、模块、Lua 或 OS loader。
- LoongArch 本次使用 QEMU 9.2.2 和固定 EDK2 固件；系统 QEMU 8.2 无法加载该 16 MiB 固件。
- 最低层文本输出用于 ASCII 诊断；完整 UTF-8 UI 与字体在后续阶段实现。

下一阶段为 Phase 2 ELF module ABI/SDK。完整调用约束及来源见
[docs/phase1.md](docs/phase1.md) 和 [CODE_ORIGINS.md](CODE_ORIGINS.md)。

## Phase 0 保留基线

保留并回归了原 Phase 0 的六配置构建、不同源码路径双构建、GRUB2 Multiboot2 hello、
X64 OVMF 测试父映像的损坏 PE 拒绝及 LoadImage/StartImage、四个 CPU 拒绝场景
（Pentium III、486、关闭 FXSR、关闭 SSE）。
原 Phase 0 IA32/ARM64/LoongArch 静态库仍仅为编译探针；
本次新增的 `boot-core.efi` 才是这些平台的可启动产物。
Phase 0 hello 返回固件仅是测试策略，不沿用到 Phase 1 或正式 loader。
Phase 0 历史摘要见 [docs/phase0-evidence.json](docs/phase0-evidence.json)，
最新回归结果在 `build/phase0/results.json`。
