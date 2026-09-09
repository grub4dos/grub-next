# Phase 2：构建与加载外部模块

Phase 2 提供无 libc 的 ELF 模块管理器、版本化 C API、注册事务和可安装的 SDK。
模块是受信任的 native code；当前只接受无签名 metadata，不实现认证或隔离。
SDK 样本通过编译期嵌入供启动验收使用，resource archive 属于 Phase 3。

## 独立构建一个模块

在 Ubuntu/WSL 的工程根目录执行。需要 CMake、Ninja、Clang 和 LLD；SDK 安装后不需要
源码、`ref/` 或 core library。下面的临时目录位于源码树外：

```sh
sdk_root=$(mktemp -d /tmp/boot-sdk-XXXXXX)
cmake -S sdk -B build/sdk -G Ninja -DCMAKE_INSTALL_PREFIX="$sdk_root"
cmake --install build/sdk
cmake -S "$sdk_root/sample" -B "$sdk_root/sample-build" -G Ninja \
  -DBOOT_SDK="$sdk_root" -DBOOT_MODULE_TARGET_ID=3 \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_C_COMPILER=clang -DCMAKE_C_COMPILER_TARGET=x86_64-none-elf \
  '-DCMAKE_C_FLAGS=-march=x86-64 -mno-red-zone' \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
cmake --build "$sdk_root/sample-build"
readelf --dyn-syms --wide "$sdk_root/sample-build/sample.so"
```

`sample.so` 注册 `answer`，执行后返回 42；同时检查已初始化数据、BSS、指针相对重定位
和浮点计算。`failing.so` 注册 `rolled-back` 后返回 `BOOT_E_IO`，用于验证原子回滚。
复制样本建立真实模块时，必须换成自己的 vendor/module UUID、名称和版本。

| Target | Metadata target ID | Clang ELF target | C flags |
| --- | ---: | --- | --- |
| i386-pc | 1 | i386-none-elf | -march=pentium4 -msse2 -mfpmath=sse |
| i386-efi | 2 | i386-none-elf | -march=pentium4 -msse2 -mfpmath=sse |
| x86_64-efi | 3 | x86_64-none-elf | -march=x86-64 -mno-red-zone |
| arm64-efi | 4 | aarch64-none-elf | -march=armv8-a |
| loongarch64-efi | 5 | loongarch64-none-elf | -march=loongarch64 -mabi=lp64d |

模块均使用 ELF ABI；X64 的 `BOOT_MODULE_CALL` 明确指定 SysV，应用在入口、init、服务
以及 core 回调上，因此可以与使用 Microsoft ABI 的 EFI core 双向调用。
IA32 使用 cdecl，ARM64 使用 AAPCS64，LoongArch64 使用 LP64D。
模块唯一动态导出为 `boot_module_entry`，其他符号 hidden。链接时 `--no-undefined`，
loader 再独立拒绝 imports、依赖、TLS、PLT、IFUNC 和非白名单 relocation。
完整 ELF 限制仍见 [ELF loader](elf-loader.md)。

## Metadata 与 ABI 协商

`PT_NOTE` 中恰好存在一个 owner 为八字节 `BOOTMOD\0`、type 为 1、descsz 为 152 的 note。
无需 section table；note header、name 和 descriptor 按 ELF 的四字节规则对齐。
所有整数为 little-endian，UUID 是不全为零的 16 字节标识，不是指针。

| Descriptor 偏移 | 长度 | 内容 |
| --- | ---: | --- |
| 0 | 16 | format=1、target、abi_min、abi_max，均为 uint32 |
| 16 | 16 | uint64 provides、requires capability 位图 |
| 32 | 32 | vendor UUID、module UUID |
| 64 | 32 | 非空、NUL 结尾的 ASCII module name |
| 96 | 16 | 非空、NUL 结尾的 ASCII version |
| 112 | 8 | uint32 hash_kind、signature_kind；本阶段只接受 0 |
| 120 | 32 | 预留 digest；本阶段必须全零 |

ABI range 必须包含 1，requires 必须是 core capability 的子集。
当前 capability 为 `BOOT_CAP_LOG=1` 和 `BOOT_CAP_SERVICE=2`，未知 provides 位拒绝。
Hash/signature 字段仅定义未签名格式；非零算法明确返回 unsupported，不能据此报告内容已认证。
Phase 3 的 manifest、Phase 15/19 的安全策略仍需后续实现。

入口无参数，返回 `const struct boot_module_v1 *`。manager 检查返回 descriptor 的范围、
对齐、abi_version、struct_size 和 init 地址，再调用 `init(const struct boot_api *)`。
API 表包含 ABI 版本、结构尺寸、capability、opaque context、日志和服务注册回调。
模块须在使用回调前检查版本、尺寸和所需 capability；后续 API 只能按协商规则扩展。
日志文本最多 1023 字节，独立于 Lua、文件系统和模块注册；注册名最多 31 个 ASCII 字符。

## 在 core 中加载

调用顺序为 `boot_modules_init` → `boot_module_inspect` → `boot_module_allocate` →
`boot_module_load` → `boot_modules_freeze`。接口在 [module.h](../include/boot/module.h)，
完整可运行调用示例在 [module_probe.c](../tests/module_probe.c)。

`boot_module_load` 接收不可变文件、互不重叠的 BL 目标存储及 cache sync 函数。
默认平台分配器在 BIOS 分配低地址可执行 BL 内存，在 EFI 调用 AllocatePages(EfiLoaderCode)
并保留 BL 记账。整个 ELF 映像目前保持可写/可执行，没有宣称逐段 W^X。
ARM64 清理 D-cache、失效 I-cache 并执行 barrier；LoongArch 使用 dbar/ibar；x86 使用
一致性指令缓存。可执行存储和 manager 必须一直保留到 handoff，不能提前释放。
没有自制 PE runtime loader；EFI image/driver 仍交固件 LoadImage/StartImage。

状态按 UUID 单调变化：未出现 → LOADING → ACTIVE 或 FAILED。最多 16 个模块、32 个服务。
入口调用前记录 LOADING；ACTIVE 和 FAILED 的重复 UUID 均拒绝，不实现重试或卸载。
校验、目标容量等入口前错误不消耗模块槽位。加载过的存储不能被后续模块覆盖。

init 注册的服务先暂存，成功才对 `boot_service_find` 可见；失败清空暂存项，已有服务保留。
递归加载、init 中 freeze、重复服务名以及 freeze 后的加载/注册均拒绝。
freeze 对应进入 MENU 前的注册表冻结。失败模块的映像保留到 handoff，避免隐式 unload；
native init 自行产生的任意硬件副作用不在注册事务回滚范围内。

| 状态返回 | 常见原因 |
| --- | --- |
| BOOT_E_INVALID | 格式损坏、UUID 重复、状态错误、名字冲突、无效 descriptor |
| BOOT_E_UNSUPPORTED | 架构/target、ABI/capability 不匹配、不支持的签名或 ELF 特性 |
| BOOT_E_NOMEM | 内存不足、模块或服务容量耗尽 |
| init 返回值 | 原样传播，撤销本次未提交注册项并记录 FAILED |

## 重现验收

```sh
python3 tools/check_references.py
python3 tools/prepare_phase1.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase2.py
python3 tests/elf.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

`tests/phase2.py` 复用 Phase 1 的九种实际启动场景，额外要求三个模块串口标记，
并比较不同源码路径的独立 runtime 构建。另将已安装 SDK 复制到 `/tmp`，对五种 target
重新独立构建两个样本，检查与实际嵌入模块逐字节相同、动态未定义 imports 为零。
`build/phase2/results.json` 保存命令、固件/映像/样本 SHA-256，串口和 QEMU 日志在同目录。
Host CTest 的 `module-abi` 覆盖元数据、能力、截断、10,000 次确定性变异、实际执行、
事务可见性、递归拒绝、回滚、重复加载和 freeze；`tests/elf.py` 以 ASan/UBSan 运行它，
并继续验证原有 ELF 非法 relocation、imports 和四种架构的 loader。

实际完成情况以 [progress.md](../progress.md) 和 [证据摘要](phase2-evidence.json) 为准。
当前纵向切片仍以验收后 panic/reset 结束，尚无菜单、archive、Lua 或 OS loader。
QEMU 验收不能替代真实硬件或 Secure Boot 验收。
