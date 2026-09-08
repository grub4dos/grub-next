# grub-next

依据 [DESIGN.md](DESIGN.md) 和 [plan.md](plan.md) 开发的新 bootloader。
当前阶段为 Phase 0：最小 Multiboot2 BIOS stage2、x64 EFI hello 与其固件测试父映像。
项目整体采用 GPL-3.0-or-later，见 [LICENSE](LICENSE)；来源见 [CODE_ORIGINS.md](CODE_ORIGINS.md)。

## 构建与验证

在 Ubuntu 24.04 / WSL Ubuntu 中安装依赖：

```sh
sudo apt-get update
sudo apt-get install -y clang lld ninja-build cmake python3 qemu-system-x86 ovmf grub-pc-bin grub-common xorriso mtools
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
```

脚本配置并构建 host 和全部五个 target，然后从不同源码路径重建、逐字节比较产物，
最后生成 GRUB2 rescue ISO 和临时 EFI FAT 盘运行 QEMU。检查失败返回非零；
日志及 SHA-256 在 `build/phase0/`。测试 ISO、ESP 和 OVMF 变量盘是临时 fixture，不属于可复现 runtime 产物。
`OVMF_CODE` / `OVMF_VARS` 可指定本地固件路径；测试使用未启用 Secure Boot 的 OVMF。
`--no-qemu` 仅用于构建排查，不能作为完整 Phase 0 验收。

单独构建：

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/host
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/bios -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/i386-pc.cmake
cmake --build build/bios
cmake -S . -B build/efi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-efi.cmake
cmake --build build/efi
```

GCC/binutils 的兼容路径目前覆盖两个 x86 可启动产物：

```sh
sudo apt-get install -y gcc gcc-mingw-w64-x86-64
python3 tests/gcc_smoke.py
```

对应 `i386-pc-gcc.cmake` 与 `x86_64-efi-gcc.cmake`；其他架构当前使用 Clang/LLD 基线，
没有把未验证的 GCC 交叉组合报告成已支持。

## 产物与限制

| 配置 | 产物 | 当前用途 |
| --- | --- | --- |
| host | `boot-info` | host 构建与错误退出基线 |
| i386-pc | `boot-stage2.elf` | GRUB2 Multiboot2 hello，COM1/debugcon |
| x86_64-efi | `boot-hello.efi` | 原生 PE32+ EFI application，COM1/debugcon |
| x86_64-efi | `boot-efi-test.efi` | 固件 LoadImage/StartImage 测试父映像 |
| i386-efi / arm64-efi / loongarch64-efi | `libboot-runtime.a` | 架构编译探针，尚不能启动 |

日志使用 x86 COM1 115200 8N1 与端口 0xE9，适用于本阶段 QEMU/PC 测试；
EFI Serial I/O protocol、屏幕输出、其他架构日志和正式入口留在 Phase 1。
BIOS 不支持无 SSE2 的 CPU，错误时在 debugcon 输出原因后 halt。
EFI hello 有意返回测试父映像，以确认 StartImage 的返回值；正式 handoff 不允许返回菜单。

所有 runtime 都不链接系统 libc。BIOS 由 `linker/i386-pc.ld` 布局，EFI 由原生 PE 链接器
设置 subsystem、入口和 relocation；没有 ELF 转 PE 的隐式布局。
源码/构建路径经 prefix-map 去除，编译拒绝日期宏，PE 时间戳固定为 0，不嵌入墙钟时间。
`SOURCE_DATE_EPOCH` 被接受并记入测试记录，runtime 不需要嵌入该值。

GRUB4DOS 的本地参考 loader 是 Multiboot1 路径，不能据此声称直接加载 Multiboot2。
当前 BIOS 启动证据来自 GRUB2；Linux boot protocol 入口和兼容加载路径属于后续工作。
参见 [progress.md](progress.md) 的验收结果与 [AGENTS.md](AGENTS.md) 的维护约定。
