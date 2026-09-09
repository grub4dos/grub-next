# 装载 ELF 模块映像

`core/elf.c` 实现 Phase 2 的 ET_DYN 映像层。它接收完整文件 buffer，校验并装载到
调用方提供的 BL 内存，返回 `boot_module_entry` 的相对位置；不调用模块代码。
该层没有认证、ABI note 协商、boot_api、注册事务、重复加载状态或卸载接口。
这些仍属于后续 Phase 2 工作，不能仅凭 ELF 校验成功执行不可信模块。

## 运行验证

在 Ubuntu/WSL、仓库根目录执行：

```sh
python3 tests/elf.py
```

需要 Phase 0 已使用的 Clang/LLD、CMake、Ninja、binutils，以及 host sanitizer runtime。
所有 fixture 由 `tests/elf_fixture/CMakeLists.txt` 构建，脚本仅编排 CMake 和验证。
日志、readelf 输出及四种架构 fixture SHA-256 保存在 `build/elf/commands.log` 和
`build/elf/results.json`，不提交二进制样本。

## 接入调用方

公开声明在 [`include/boot/elf.h`](../include/boot/elf.h)。

1. 调用 `boot_elf_inspect(file, length, machine, &image)`，取得 `size`、`alignment`、
   `entry_offset`。`machine` 必须由可信的 target 配置选择，不能从输入文件决定执行架构。
2. 分配至少 `size` 字节、满足 `alignment` 的可直接寻址 BL buffer。
3. 调用 `boot_elf_load(file, length, machine, buffer, capacity, &image)`。
   loader 再次完整校验；成功前不修改目标 buffer 或结果 descriptor。
4. 后续模块管理层完成信任、note/ABI/capability 校验，由平台落实执行权限、必要的
   instruction-cache 同步，再按 ELF 架构 ABI 调用 `buffer + entry_offset`。
   x64 EFI core 的 Microsoft ABI 不能直接当作 ELF SysV ABI 使用。

文件在每次调用期间必须保持不变，输入必须与目标区域分离。结果 descriptor 应是独立
调用方对象。失败不分配资源；成功后内存归调用方管理，映像层不维护模块状态。
`size` 包含段间空洞；返回位置以向下对齐的最低 PT_LOAD 虚拟地址为零点。
i386 装载 buffer 必须完全位于 32 位可寻址范围，不能截断指针。

| 返回值 | 含义 |
| --- | --- |
| `BOOT_OK` | 完整校验/装载成功 |
| `BOOT_E_INVALID` | 截断、越界、溢出、布局不合法、缺少必需表或目标 buffer 不合法 |
| `BOOT_E_UNSUPPORTED` | 错误架构/类型、非白名单 ELF 特性或符号/重定位策略不满足 |
| `BOOT_E_NOMEM` | 目标 buffer 容量不足 |

## 生成符合约束的 ELF

本次使用严格的模块 ELF profile，而非通用 Unix 动态链接器：

- ELF32 i386 用于 `i386-pc` / `i386-efi`；ELF64 用于另外三个 target。
  仅 little-endian、SysV OSABI，LoongArch 要求 ABI v1 / LP64D 标志 `0x43`。
- PIC、hidden visibility、无 libc/隐式 runtime；动态符号只公开一个全局函数
  `boot_module_entry`，ELF `e_entry` 必须指向同一函数。入口必须位于文件支持的 RX 段。
- 使用 `-Bsymbolic --hash-style=sysv -e boot_module_entry`；关闭 unwind tables、
  stack protector 和 build-id。测试 fixture 的入口返回整数，仅用于执行校验，
  **不是** DESIGN.md §9.1 的正式 module descriptor ABI。
- 必须有 PT_LOAD、单个 PT_DYNAMIC、DT_HASH、DT_SYMTAB、DT_STRTAB 及大小字段。
  不依赖 section headers，移除 section table 后仍可装载。
- 最多 32 个 program headers，映像跨度与最大段对齐各不超过 16 MiB；
  hash bucket、symbol、relocation、dynamic entry 计数最多 65,536，符号名最多 255 字节。
  段间不得重叠，filesz 不得大于 memsz，段对齐必须为 0/1 或符合 ELF 同余关系的二次幂。
- 拷贝 PT_LOAD 的文件内容，清零 BSS 和空洞。拒绝 W+X 段和 executable stack；
  这些是文件策略校验，不代表本层已经建立硬件 W^X 页权限。PT_GNU_RELRO 作为布局
  信息接受，本层不改变页表。平台执行权限与缓存处理仍需由调用方接入。

| 架构 | 重定位表 | 唯一允许的重定位 |
| --- | --- | --- |
| i386 | REL | R_386_RELATIVE (8) |
| x86_64 | RELA | R_X86_64_RELATIVE (8) |
| ARM64 | RELA | R_AARCH64_RELATIVE (1027) |
| LoongArch64 | RELA | R_LARCH_RELATIVE (3) |

所有重定位必须无符号引用、按目标地址递增排列、互不重叠、按字长对齐，并完整落在 RW 段。
addend 必须指向本映像已装载区域；不支持负 addend、外部地址或 one-past-end 指针。
i386 隐式 addend 从原文件中的完整 4 字节读取。64 位显式 addend 来自 RELA。
实际重定位始终读取不可变输入文件，防止先前修补改写后续重定位表。

拒绝 undefined imports（包括 weak）、额外可见导出、TLS、IFUNC、PT_INTERP、DT_NEEDED、
PLT、text relocation、REL[R]/RELA 格式混用、constructor/destructor、symbol versioning、
GNU-hash-only 以及其他非白名单 dynamic tags。REL[A]COUNT 只作为可忽略提示；
不会据此跳过任何重定位校验。

## 验收证据与边界

- `tests/elf_test.c` 在 x86_64 Linux host 实际执行链接生成的 ET_DYN：初始化数据与
  BSS 指针均经过 RELATIVE 修补，入口返回 42；重新装载后 BSS 再次归零。
- 同一测试覆盖 section-table-free 文件、逐字节截断、容量/对齐错误、错误架构、
  TLS/interpreter/import/IFUNC、非法重定位、重复/乱序目标和 10,000 次确定性变异。
  ASan/UBSan 运行通过，拒绝路径检查目标 buffer 未写入。
- `tests/elf.py` 为四种 ELF 架构生成真实链接产物，在 host 上分别装载，以独立逻辑
  检查每个重定位值和 BSS。i386 测试使用低地址映射；ARM64/LoongArch 未在此测试中执行。
- loader 编入五个 target 的共用 runtime。Phase 0/1 QEMU 回归检验原有启动路径，
  **不代表固件内模块执行已验证**。没有声称完成所有 Phase 2 验收项。

本次代码为项目新实现，没有从 `ref/` 迁移代码或更新来源锁。
