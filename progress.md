# 实施进度

更新：2026-09-10。Phase 4 只读存储切片已实现；Phase 1/2/3 平台、内存、模块和资源持续回归，
全部平台入口、模块与存储读取均有实际 QEMU 启动证据。
连续跨 4 GiB RAM 的测试范围见下文；未将普通 PC 的保留区当作可写 RAM。

2026-09-09 修复 GitHub Actions 的 Phase 1 准备步骤：QEMU 9.2.2 源码归档包含一个指向
`/opt/X11/include` 的无关绝对符号链接，Python 3.12 的 `tarfile.data_filter` 会拒绝它。
`tools/prepare_phase1.py` 现在只跳过该固定、未使用的成员，其他条目继续执行安全过滤；源码先在
临时目录完整解包、校验 `configure` 和归档 SHA-256 后再发布，并用 `.boot-source-complete`
标记避免复用不完整解包。全新临时目录解包测试已通过。

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

Phase 2 ELF module ABI/SDK 的 ET_DYN 映像 loader 已完成，见下文。Phase 1 完整调用约束及来源见
[docs/phase1.md](docs/phase1.md) 和 [CODE_ORIGINS.md](CODE_ORIGINS.md)。

## Phase 2：ELF ET_DYN loader（2026-09-09）

本次完成 `plan.md` 的“实现 ELF ET_DYN loader”项。

- `core/elf.c` / `include/boot/elf.h`：无 libc 的 inspect/load API；ELF32/64 little-endian
  四种机器类型覆盖五个 target，完整校验 PT_LOAD / PT_DYNAMIC / SysV hash / dynsym，
  装载文件段、清零 BSS/空洞并定位唯一 `boot_module_entry`。
- 最小白名单为 i386 REL 和其余架构 RELA 的 RELATIVE；拒绝外部 imports、依赖、TLS、
  IFUNC、PLT、text relocation、非法/重复/乱序修补位置及越界/溢出输入。
- 校验成功前不写目标；调用方提供可寻址 BL buffer，负责后续执行权限和 I-cache 同步。
  loader 不调用入口、不分配内存、不提供卸载接口。profile 的全部限制见
  [docs/elf-loader.md](docs/elf-loader.md)。
- CMake 动态生成真实 ELF fixture，未提交二进制；所有 target 共用同一 loader 源码。
  i386 运算不引入 `__udivdi3` / `__umoddi3` 等 host/runtime helper。

本次已执行（Ubuntu/WSL）：

```sh
python3 tests/elf.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase1.py
python3 tests/gcc_smoke.py
git diff --check
```

结果：host 五项 CTest 全部通过；x86_64 真实模块实际执行返回 42，验证初始化数据、
两处指针重定位和 BSS。无 section table 的文件、逐字节截断和 10,000 次确定性变异
通过 ASan/UBSan；拒绝路径确认目标内存保持原值。四种 ELF 架构的真实链接产物均在
host 完成装载和独立重定位/BSS 检查。日志与 SHA-256 在 `build/elf/commands.log`、
`build/elf/results.json`；Phase 0/1 原始串口和 results.json 仍在各自目录。

Phase 0 全套回归、Phase 1 九个 QEMU 场景与独立源码路径双构建通过；GCC 兼容回归通过。
命令输出另保留在 `build/elf/{phase0,phase1,gcc-smoke}.log`。
QEMU 仅回归原有启动路径，没有在固件中调用新 ELF 入口；跨架构 fixture 装载也不等于
跨架构执行。没有声称 SDK 样本已在所有固件 target 运行。

上述 2026-09-09 交付当时尚缺 ELF note/UUID/ABI/capability 校验、boot_api、注册事务、
重复加载状态、正式模块调用的平台适配和 external SDK；现已在下面的 2026-09-10 交付补齐。

## Phase 2 完成交付（2026-09-10）

- `core/module.c`：严格 PT_NOTE 遍历和 BOOTMOD metadata；校验 UUID、target、ABI range、
  capabilities、名称/版本与未签名格式。唯一入口返回 descriptor，校验后通过 boot_api 调用 init。
- `include/boot/module.h`：版本、struct_size、capability 协商，日志与服务注册回调。
  X64 显式 SysV ABI，解决 EFI Microsoft ABI 与 ELF 模块的双向调用。
- 注册事务只在 init 成功后公开服务；失败清空本次注册，已提交服务不受影响。
  LOADING/ACTIVE/FAILED 单调状态、按 UUID 拒绝重复加载、递归拒绝及 MENU 前 freeze；无依赖、无 unload。
- `platform/module.c`：BIOS 低地址 BL 映像存储、EFI AllocatePages(EfiLoaderCode)，
  ARM64 D/I-cache 同步和 LoongArch dbar/ibar；已加载或初始化失败的映像保留到 handoff。
- 安装式 `sdk/`：仅公开头文件、CMake helper 与两个样本，无 core library/imports。
  成功样本检查数据、BSS、RELATIVE 重定位和浮点计算；失败样本注册后主动返回 BOOT_E_IO。
- `tests/phase2.py` 继承 Phase 1 全部启动场景，另要求模块执行标记；SDK 复制到源码树外 `/tmp`
  后重新构建，并核对与实际嵌入样本逐字节相同。构建仍全部由 CMake 描述。

验收结果：

| 项目 | 证据与边界 |
| --- | --- |
| 五个 target 加载/执行 | 九个 QEMU 场景通过；BIOS MB2、Linux protected/setup 和四种 EFI 均执行模块服务返回 42 |
| 树外 SDK | 五种 target × 两个样本，独立构建逐字节相同；readelf 确认动态未定义 imports 为零 |
| 失败原子性 | host 和全部固件路径检查 rolled-back 服务消失，既有 answer 服务仍可调用 |
| 重复/冻结/递归 | ACTIVE/FAILED UUID 再加载拒绝；freeze 后拒绝加载/注册；host 检查 init 中递归及事务未提交不可见 |
| 非法输入/ABI | 六项 host CTest 通过 ASan/UBSan；metadata/header、ABI range、descriptor ABI、API struct_size、能力、截断与 10,000 次 note 变异；沿用 ELF 非法 relocation/imports 测试 |
| 可复现 | 不同源码路径独立构建的七个 runtime/host 产物相同，另比较十个树外 SDK 模块 |
| 回归 | Phase 0 全套、Phase 1 全部场景（Phase 2 套件复用）、四架构 ELF fixture 与 GCC x86 启动通过 |

Ubuntu/WSL 实际命令：

```sh
python3 tools/check_references.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase2.py
python3 tests/elf.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

原始串口、QEMU 命令、固件和映像摘要在 `build/phase2/`；独立 SDK 校验与 SHA-256 在
`build/phase2/results.json`。完整运行输出在 `build/phase2-full.log`，回归输出分别为
`build/phase2-{elf,phase0,gcc}.log`；sanitizer/CTest 命令日志在 `build/elf/commands.log`。
可持久审查摘要见 [docs/phase2-evidence.json](docs/phase2-evidence.json)，
SDK/API 与调用约束见 [docs/phase2.md](docs/phase2.md)。CI 已接入同一测试，未声明远端 Actions 已执行。

边界：当前模块为受信任 native code，hash/signature metadata 只接受明确的未签名格式，
不声称密码学认证或逐段 W^X。上述 Phase 2 交付使用直接嵌入样本；现已由下文 Phase 3
CPIO/manifest 取代；
真实硬件、Secure Boot、Lua、菜单和 OS handoff 尚未实现/验证。未改 ref/、来源锁，未提交或推送。

## Phase 3 完成交付（2026-09-10）

- `core/archive.c`：参考锁定 GRUB `newc.c` / `cpio_common.c` 的格式布局，新写无 libc
  的有界只读 reader。完整 archive 校验后公开索引；严格 hex、长度、trailer、路径、
  重复项和类型检查。上限为 16 MiB / 128 条记录 / 255 字节路径。
- `tools/pack_resources.py` / CMake：为每个 target 生成 `resource.cpio` 和 SHA-256
  内容清单。路径排序、固定 metadata、零 mtime、稳定 inode；不带入 host 路径和时间。
  样本模块、默认 `boot.lua`、原创最小诊断字体随 archive 打包。
- `platform/resource.c`：EFI 直接使用只读 `.bootres` section；BIOS 读取第一个
  Multiboot2 module 或 Linux 32 位 initrd，经 physical API 复制到 BL 内存。
  无外部输入时使用内嵌 archive；无效/空输入、OOM、读取或 hash 失败不静默回退，
  失败释放副本。模块在完整 archive/manifest 校验后才进入 Phase 2 loader。
- LoongArch 构建期 PE 转换保留独立只读不可执行资源 section，更新 section 布局和
  PE size accounting。四种 EFI 的 resource section 均与独立 archive 逐字节一致。
- `tests/phase3.py` 复用 Phase 1/2 的实际执行/事务/SDK 验收；CI 已切换到包含它们的
  Phase 3 超集，并保留 Phase 0、GCC、sanitizer job 和资源证据上传。

实际验收结果：

| 项目 | 结果与边界 |
| --- | --- |
| 五个 target / 三种输入 | 12 个 QEMU 场景通过：原 9 个平台/内存/模块场景使用外部 BIOS 资源或 EFI 内嵌资源，另增加无磁盘 BIOS 内嵌资源和两种损坏输入拒绝 |
| BIOS 无文件系统资源消费 | SeaBIOS `-kernel`，分别有/无 `-initrd`，不连接磁盘/CD；配置和字体取得成功，archive 内模块实际执行返回 42 |
| EFI 资源 | IA32/X64/ARM64/LoongArch64 固件加载并执行模块；只读 `.bootres` 字节和资源清单匹配 |
| 可复现 | 不同源码路径独立构建比较 7 个既有 runtime/host 产物、6 份 archive、6 份 manifest；五种 target 的 10 个树外 SDK 模块相同 |
| Host / sanitizer | 9/9 CTest 通过 ASan/UBSan；逐字节截断、10,000 次变异、独立 hashlib、路径/链接/重复/溢出/清单异常，另有 OOM/I/O/空输入和失败清理 |
| Packer / 互操作 | 命令行改变来源路径、mtime、参数顺序和 epoch，产物相同；本机 GNU cpio 双向读取，提取内容逐字节一致 |
| 回归 | Phase 0 全套和 GCC x86 实际启动通过；引用检查、格式和 `git diff --check` 通过 |

Ubuntu/WSL 实际命令：

```sh
python3 tools/check_references.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase3.py
cmake -S . -B build/p3-dev -G Ninja -DCMAKE_C_COMPILER=clang -DBOOT_SANITIZERS=ON
cmake --build build/p3-dev
ctest --test-dir build/p3-dev --output-on-failure
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

原始串口、QEMU 命令、固件/产物摘要及 SDK 对比在 `build/phase3/` 和
`build/phase3/results.json`。全套输出在 `build/phase3-full.log`；回归与 sanitizer 输出在
`build/phase3-{phase0,gcc,sanitizers}.log`。持久摘要见
[docs/phase3-evidence.json](docs/phase3-evidence.json)，接口和格式见
[docs/phase3.md](docs/phase3.md)。这里只声明本地结果，未声明远端 Actions 已执行。

边界：CPIO 只接受限定的未压缩 `070701` profile，不支持 symlink、硬链接、设备节点、
CRC newc 或拼接 archive。Lua 是源文件资源，尚不执行；字体仅为空格、问号、数字和大写
拉丁字母的 5×7 子集，尚不渲染。manifest 用于损坏检测，没有密码学认证。
资源输入未增加任意 Linux 高地址 initrd 协议支持；没有声称真实硬件、Secure Boot、
磁盘文件系统、菜单或 OS handoff 已完成。未改 ref/ 或来源锁，未提交或推送。

## Phase 4 原样移植交付（2026-09-10）

- `include/boot/storage.h` / `core/storage/`：原生块 provider、只读 slice/filter、分区和
  文件接口；64 位 byte/LBA/size，显式 `boot_status_t`，固定缓存与 generation，不恢复全局 errno。
- 按用户维护性要求移除重写的四个 fs reader，使用 vendor/grub 中的原样 FAT、ISO9660、NTFS、
  ext2、ntfscomp、fshelp。22 个原样文件逐文件 SHA-256 锁定；仅两个可审查 bug fix 在 build 应用。
- core/grub 集中处理私有环境/API 适配：单次 operation error、嵌套恢复、限额分配与回收，
  原生块到 512-byte sector adapter。公开 API 不暴露全局 grub_errno。
- diskfilter/LVM/RAID 九个源文件原样预留，不编译、不声明 Phase 5 完成。
  GPT/MBR/EBR 和 firmware providers 保留本工程实现，未迁移 nativedisk/controller drivers。
- BIOS EDD/CHS provider，低地址 BL thunk/bounce，逐次校验 CR0/CR3/CR4 和 eager FP 恢复。
  识别 EDD 对空托盘/ATAPI 的未知容量；CHS 软盘、EDD HDD/CD 有真实 QEMU 读取。
- EFI LocateHandleBuffer/Block I/O provider：保留 device path hash、MediaId、native block
  geometry；由 core 解析分区，rescan 或 media change 后不复用旧 handle/cache。
- host file-backed shim 和 `boot-storage-read`；所有 runtime 与 host 由 CMake 编译同一 reader。
  修正 EFI Clang/GCC 的 host stack-probe 依赖，并允许 BIOS 输入信息复用已覆盖它的 BL 保留区。
- CI 切换到 Phase 4 超集，增加格式工具与 storage sanitizer 流程。无提交或推送，无远端 CI 运行声明。

验收结果：

| 项目 | 证据与边界 |
| --- | --- |
| 五个 target | 27 个 QEMU 场景，包括既有 12 个启动/资源场景、11 个 BIOS 存储场景、4 个 EFI 存储场景；按串口标记及 reset 退出判定 |
| native block | host 512/2048/4096，EFI 实际 2048/4096 Block I/O；未对齐读取、分区偏移和 transfer/alignment 限制通过 |
| 文件内容 | 14 个动态格式映像；12 MiB+137 文件完整 SHA-256，空/全零文件、Unicode 名称、目录、重排的碎片 FAT 链、ISO multi-extent |
| 大型稀疏文件 | ext2/3/4 和 NTFS 的 5 GiB+26 逻辑文件；host 与五个 target 校验 5 GiB 偏移处的 hole/tail，未完整搬运 5 GiB |
| stale 状态 | 全部 firmware target 实际 rescan 后旧句柄拒绝；host EFI mock 注入 MediaId 更换、空介质和枚举失败，不等同真实硬件热插拔 |
| host 拒绝/诊断 | 12 项 CTest、12 个具名损坏场景、20,000 次有界 metadata 变异通过 ASan/UBSan；包括 FAT 环路、NTFS USA、GPT CRC、EBR 环路与溢出 |
| 可复现与回归 | 独立源码路径的 runtime/resource/manifest/host storage 产物相同，10 个树外 SDK 模块相同；Phase 0、GCC x86 实际启动、引用与格式检查通过 |

Ubuntu/WSL 实际命令：

```sh
python3 tools/check_references.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase4.py
python3 tests/storage.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

原始串口、QEMU 命令、固件/产物摘要、独立路径与 SDK 比较在 `build/phase4/`；
sanitizer、内容 SHA-256、具名损坏和变异记录在 `build/storage/`。
完整输出为 `build/grub-import-phase4.log`、`build/grub-import-storage.log`、
`build/grub-import-phase0.log` 和 `build/grub-import-gcc.log`。持久摘要见 [docs/phase4-evidence.json](docs/phase4-evidence.json)，
接口/资源上限/格式 profile 见 [docs/phase4.md](docs/phase4.md)。

边界：exFAT 未编译；上游 NTFS 压缩/attribute-list、ext meta_bg/symlink、ISO SUSP CE/symlink
代码保留，但仅对 docs/phase4.md 所列媒体提供读取证据。不回放 journal，不校验 ext metadata checksum。
GPT 只接受有效 primary header/entries，不自动从 backup 恢复；其他历史 partition maps 未移植。
BIOS drive number 不是稳定硬件 identity；EFI path identity 不能代替介质 UUID。
未声明真实硬件、Secure Boot、OS handoff、INT 13h/EFI map、LVM/RAID/cryptodisk、Lua 或写入完成。
ref/ 和来源锁未改动；vendor 保留原格式，Allman 格式检查仅应用本工程代码。

## Phase 5 loopback / diskfilter 切片（2026-09-11）

按用户优先级完成 loopback 与 diskfilter 的导入和实际接入；不将整个 Phase 5
（仍含 cryptodisk）标记为完成。

- `vendor/grub/` 新增原样 `loopback.c`、`kern/list.c`、`list.h`、`lvm.h`，
  共 26 个原文件有逐文件 SHA-256；既有 diskfilter、LVM、MD 1.x 和 RAID5/6 recovery
  静态编入全部 target。来源仍为 `2f972128c48b90bf8b63aadffe6d546976e1dee6`。
- `core/grub/volume.c` / `include/boot/volume.h` 提供 add、scan、list、open、reset 和
  保守 physical-traceability 查询；适配集中于私有层，不移植旧 parser 或模块依赖。
  session 复制 file/fs 句柄；公开错误仍显式返回，不恢复全局 grub_errno。
- native block 层使用逐 slot active mask 和对齐 scratch，允许合法嵌套读取并防止
  cache collision。已有分区 parser 同时用于物理来源和 loopback/组合卷。
- 新增唯一上游补丁 `0003-diskfilter-lv-cycle.patch`：动态双 LV 相互引用在补丁前
  复现 ASan stack-overflow，补丁后按 16 层上限拒绝；vendor 字节保持不变。
- BIOS 空/不可读候选在扫描时跳过；已公开来源保持 generation/validate 检查。
  loopback 和组合卷不能直接作为物理 blocklist 导出；不添加写入、解压或 map。

实际验收结果：

| 项目 | 证据与边界 |
| --- | --- |
| 新增五目标启动 | 15 个 QEMU 场景全部通过：每个 target 各验证双层 loopback、LVM-on-MD、缺一盘 RAID5，以及 reset/旧句柄失效；EFI fixture 使用 4096-byte Block I/O |
| Phase 4 回归 | 原有 27 个启动场景通过；不同源码路径的 runtime/resource/host 产物相同，五 target 树外 SDK 仍通过 |
| Host 内容 | 36 组（12 场景 × 512/2048/4096）；完整 131209-byte 内容逐字节一致，SHA-256 为 `f871df54997e4d27c096f10362f55a7677479c91d50c08af142975382241cd7f` |
| Host 嵌套/生命周期 | 双层文件映像、loopback→MBR→LVM、LVM-on-MD、调用方 file/fs 重用、重复名、末扇区补零、非对齐/cache collision、来源关闭后的 cache-hit stale、reset 后再建会话 |
| 拒绝 | 8 个具名损坏场景通过 ASan/UBSan：MD 盘数/role/偏移/版本，LVM label offset/metadata offset/size 和双 LV 环 |
| 基线 | 12 项 host CTest、既有存储内容/12 类损坏/20000 次变异、Phase 0 和 GCC BIOS/Linux/X64 EFI 实际启动通过 |

Ubuntu/WSL 实际命令：

```sh
python3 tools/check_references.py
python3 tools/import_grub.py --import
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase5.py
python3 tests/storage.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

完整主日志 `build/p5-phase5.log`；新增固件串口与命令在 `build/phase5/`，
host 内容和拒绝证据在 `build/volumes/`，此前层次回归仍在 `build/phase4/`、
`build/storage/`、`build/phase0/`、`build/gcc-smoke/`。
额外回归总日志为 `build/p5-{storage,phase0,gcc}.log`；循环缺陷原始复现保留于
`build/p5-cycle-before.log`。持久摘要见 [docs/phase5-evidence.json](docs/phase5-evidence.json)，
接口和范围见 [docs/phase5.md](docs/phase5.md)。CI 已改为同一 Phase 5 超集，未声称远端运行。

边界：本次是动态格式夹具与 QEMU 验收，不是生产阵列/真实硬件验证。LVM 测试限于
单 PV 非连续 segment 及上述嵌套；MD 测试涵盖 1.0/1.1/1.2、RAID0/1/5，
RAID6 recovery 虽已链接，但没有 RAID6 媒体验收。MD 0.90、NVIDIA RAID、LDM
仍仅预留；cryptodisk、透明解压、blocklist 实体化、写入、map、Secure Boot 和 OS handoff
未完成。loopback 和组合卷的保守 physical=false 不等于已实现 blocklist 算法。
ref/、来源锁未改动，未提交或推送。

## Phase 0 保留基线

保留并回归了原 Phase 0 的六配置构建、不同源码路径双构建、GRUB2 Multiboot2 hello、
X64 OVMF 测试父映像的损坏 PE 拒绝及 LoadImage/StartImage、四个 CPU 拒绝场景
（Pentium III、486、关闭 FXSR、关闭 SSE）。
原 Phase 0 IA32/ARM64/LoongArch 静态库仍仅为编译探针；
本次新增的 `boot-core.efi` 才是这些平台的可启动产物。
Phase 0 hello 返回固件仅是测试策略，不沿用到 Phase 1 或正式 loader。
Phase 0 历史摘要见 [docs/phase0-evidence.json](docs/phase0-evidence.json)，
最新回归结果在 `build/phase0/results.json`。
