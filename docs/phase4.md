# Phase 4：只读存储核心

同一套 GRUB reader 经私有兼容层接入 host 文件、BIOS INT 13h 和四种 EFI Block I/O target。
本阶段交付的是有明确格式边界的存储纵向切片，启动后仍执行探针并 panic/reset。
没有原生 ATA/AHCI/USB controller driver、文件系统写入、Lua 菜单或 OS handoff。

## 源码维护方式

`vendor/grub/` 是锁定 GRUB 提交的原样快照，22 个文件都与来源逐字节一致。
六个 fs 源文件用于当前阶段，九个 diskfilter/LVM/RAID 源文件只预留给 Phase 5，
另有七个原样头文件。完整清单与 SHA-256 在 `vendor/grub/sources.json`。

```sh
python3 tools/import_grub.py          # 无需 ref/ 的快照校验
python3 tools/import_grub.py --import # 先验证来源锁，再原样复制选定文件
```

更新上游时单独审查来源锁和清单，再检查补丁是否仍必要。不要格式化 vendor 文件，
不要对 GRUB parser 做 boot API 风格改写，也不要修改 ref/ 来修复构建。
`core/grub/include/grub/` 只提供私有环境接口，其余头文件使用原样 vendor 版本。
CMake 校验快照后，在 build/grub-fs 中准备六个编译输入；构建不依赖 ref/。

当前只有两个独立 bug fix：ext4 空 extent tree/首 extent 前 hole 返回零，
以及 FAT 自环拒绝；对应 `patches/grub/0001-*`、`0002-*`。每个补丁附复现说明，
没有在其中混入接口适配。原先重写的四个 reader 已删除。

## 复现读取与验收

在 Ubuntu/WSL 中准备 README 所列编译器、固件与 QEMU，另安装格式工具：

```sh
sudo apt-get install -y dosfstools ntfs-3g e2fsprogs xorriso mtools patch
python3 tools/check_references.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase4.py
python3 tests/storage.py
```

`phase4.py` 包含 Phase 1/2/3 启动、资源和外部 SDK 验收，增加存储启动场景；
`storage.py` 在 CMake 的 ASan/UBSan host 构建中执行内容比对、具名损坏测试和变异测试。
测试使用 mkfs、mtools、e2fsprogs、ntfs-3g 和 xorriso 动态生成映像，不需要挂载、root
或仓库中的二进制 fixture。这里只声明本地执行结果，不声明远端 CI 已运行。

读取 GPT 分区中的文件，或读取超过 4 GiB 的文件偏移：

```sh
build/storage/host/boot-storage-read build/storage/fixtures/gpt4096.img 4096 /probe.bin 1 > build/probe.bin
cmp build/probe.bin build/storage/fixtures/source/probe.bin
build/storage/host/boot-storage-read build/storage/fixtures/ext4.img 512 /sparse.bin 0 5368709120 > build/sparse-tail.bin
```

参数依次是映像、设备逻辑块大小、绝对文件路径、可选分区编号、可选文件字节偏移。
分区编号 0 表示整个设备，MBR 主分区/GPT 从 1 开始，MBR logical partition 从 5 开始。
路径指向目录时列出目录；失败返回非零退出码和数值 `boot_status_t`。

## 调用接口与生命周期

公开头文件为 [storage.h](../include/boot/storage.h)；host 文件 provider 使用
[host_block.h](../include/boot/host_block.h)。所有 I/O 接口显式返回状态，不使用全局错误变量。

| 操作 | 契约 | 失败时的状态 |
| --- | --- | --- |
| `boot_storage_rescan` | 单线程、非 I/O 期间调用；先增加 generation，再清空设备与缓存 | generation 不回绕；旧句柄永远不能用于新一代设备 |
| `boot_block_add` | 注册原生 logical/physical block size、alignment、max_blocks 和 provider | 不接受零容量、乘法溢出和无效几何参数 |
| `boot_filter_slice` | 有界只读 offset/length filter；保留根 provider、代次、绝对偏移和深度 | 拒绝越界及超过 16 层；没有写/COW/map exporter |
| `boot_partitions` | GPT、MBR primary 和 extended/EBR；成功后才公开完整数组 | CRC、重复 GUID、重叠、范围或 EBR 环路错误不留下部分结果 |
| `boot_fs_mount` | 调用方持有 `boot_fs`，得到 root 与文件系统 identity | 全部驱动拒绝时返回 CORRUPT；provider 的 STALE/IO/TIMEOUT 保留 |
| `boot_file_open/list` | `/` 路径、UTF-8 名称，路径最长 4095 字节；沿用 fshelp 路径解析；目录回调返回显式状态 | `NOT_FOUND`、`UNSUPPORTED`、`CORRUPT`、I/O 错误分别返回 |
| `boot_file_read` | 64 位字节偏移，EOF 成功返回 0 字节；稀疏处理沿用上游 reader | 不允许 off 大于文件长度；失败时 got=0，buffer 可能已部分写入 |

`boot_storage` 必须零初始化并保持地址稳定。slice 是值句柄；文件借用 fs，fs 借用 storage。
存在文件引用时不得覆盖、复制到新地址或重新 mount 同一个 fs 对象。host file provider
在使用期间保持打开；改写后必须显式 rescan，关闭后的旧句柄返回 STALE。
没有动态模块注册接口、卸载、后台线程或并发 I/O。目录回调可执行嵌套文件操作；
底层 provider 进行中的 I/O 不允许重入。文件句柄保存路径和 generation；每次 read 在单次
operation 内重新 open/read/close，不让上游指针逃逸。这样可能重复读取元数据，未声称性能提升。

私有 `grub_errno` 宏指向当前 operation 的错误槽，外层状态在嵌套调用后恢复；公开 API 仍显式
返回状态。每个 scope 回收其余分配，错误路径也不留下分配。总 payload 限额 4 MiB，
runtime 使用 5 MiB BL arena（含分配头与碎片余量），host 使用独立 malloc allocation 供 ASan 检查。
每次 operation 最多 1,048,576 次 disk read 调用；这不是完整的恶意文件系统隔离保证。

缓存固定为 8 条，每条保存一个原生逻辑块，最多 32 KiB 数据；对齐与 bookkeeping 另占空间。
每次读取先检查 generation 和 provider media，再查缓存。底层调用每次一个原生块，
使用 4096 对齐的内部 buffer，不会超过 provider 的 transfer 上限；上层可以读取任意未对齐字节。
读取失败或 media 校验失败清空缓存。物理块大小和最低对齐位置保留为元数据，不混作 LBA 单位。

## 固件与身份

BIOS 使用 EDD AH=41h/48h/42h，以及 CHS AH=08h/02h fallback；CHS 逐扇区读取，最多重试三次。
低地址 64 KiB BL 区域容纳 thunk、临时 GDT、栈、DAP、FXSAVE 区和 bounce buffer。
thunk 要求本工程的 flat protected-mode、分页关闭入口；进入固件时切换 real mode，
返回恢复 CR0/CR3/CR4、GDT/IDT、栈和 eager FP。每次调用比较恢复后的 FXSAVE 内容与控制寄存器。
ATAPI EDD 报告未知容量时只为可识别 ISO volume 取得容量，空托盘不注册。
INT 13h 不接受高地址 buffer；这里的 bounce 是 transport 输入，后续高内存消费仍走 Phase 1 physical API。
没有安装 INT 13h map handler，也不声称可跨 OS handoff 保留此 BL thunk。

EFI 使用 LocateHandleBuffer/HandleProtocol 枚举 Block I/O，排除固件的 logical-partition child，
由同一 core 解析磁盘分区。ReadBlocks 使用枚举时的 MediaId，缓存命中前也检查 present、MediaId、
块大小和 LastBlock；重新枚举失败同样让旧 handle/cache 失效。协议仅在 Boot Services 期间有效。
本阶段没有 EFI driver LoadImage/ConnectController 流程，也没有 EBS 后 Block I/O callback。

EFI provider identity 是完整、限长 device path 的 SHA-256，描述连接路径而非保证介质唯一。
BIOS drive number 仅为会话别名，`identity_stable=0`；不会把 hd0 当永久硬件 identity。
GPT 保存 disk GUID 与 partition GUID；MBR 保存磁盘 signature 和分区编号；FAT serial、
NTFS serial 和 ext UUID 由上游 fs_uuid 转成文本放入 fs.uuid；ISO 沿用上游创建时间派生标识，
它不是标准、唯一的介质 UUID。不可获得标识时保留空字符串。
换盘后应重新按文件系统 UUID/GPT GUID 选择对象，不能仅凭 EFI 连接路径认定是原来的介质。

## 格式与验证范围

解析规则来自锁定版本的 GRUB，不再维护另一个手写子集。已启动和校验的样本如下：

| Reader | 本次实际样本 | 仍未据此证明的变体 |
| --- | --- | --- |
| FAT | FAT12/16/32、VFAT Unicode、碎片链、目录、空文件 | exFAT 未编译；任意复杂损坏链不能仅凭自环测试声称全部拒绝 |
| ISO9660 | primary/Joliet、Rock Ridge 生成映像、multi-extent、目录 | 上游 SUSP CE/symlink 代码保留，但本次没有专门覆盖所有 RR 扩展 |
| ext2/3/4 | indirect/extent 文件、完全 sparse、5 GiB hole/tail、目录 | 上游 meta_bg/symlink 代码保留但未覆盖所有变体；不回放 journal、不验证 ext metadata checksum；未证明 unwritten extent 数据语义 |
| NTFS | resident/nonresident、Unicode、USA、目录、5 GiB sparse tail | attribute-list、NTFS 压缩 reader 已保留并编译，尚无专门压缩/跨记录媒体证据；无加密或 ADS 选择 API |
| Partition | GPT、MBR primary、extended/EBR，512/4096 native-LBA | 保留本工程有界实现；不自动恢复 backup GPT，不含其他历史 maps |

GPT/MBR 输出最多 128 项；GPT entries 最多 128，entry stride 为 128 的倍数且不超过 4096。
损坏测试验证实际读取边界：ext 加密文件标志、NTFS attribute 起点越界和 ISO descriptor magic。
卷的 ext encryption feature bit 本身不禁止普通文件读取；NTFS bytes-in-use、ISO 根记录长度等
冗余字段不是本版本上游 reader 的全部强制检查项。变异测试通过不等于全部磁盘损坏都能检测。

## 验收证据

持久摘要见 [phase4-evidence.json](phase4-evidence.json)；原始输出保存在 build，未提交二进制 fixture。

| 证据 | 验证结论 | 重跑入口 |
| --- | --- | --- |
| `build/phase4/results.json`、串口日志 | 27 个 QEMU 场景：原 12 个场景、11 个 BIOS 存储场景、四个 EFI 存储场景 | `tests/phase4.py` |
| 各 EFI storage 串口及 BIOS ext/NTFS 串口 | 同一 reader 读取 5 GiB 偏移：ext 的 hole+tail、NTFS 的未初始化 sparse tail | `tests/phase4.py` |
| `build/storage/results.json` | 14 份格式工具映像；12 MiB+137 大文件全内容 SHA-256、空文件、Unicode、目录、碎片 FAT 和 multi-extent ISO | `tests/storage.py` |
| `build/storage/commands.log` | 12 项 CTest；12 个具名损坏拒绝、20,000 次确定性变异通过 ASan/UBSan | `tests/storage.py` |
| 独立临时源码目录双构建与树外 SDK | runtime、archive、manifest、host storage 工具逐字节相同；10 个 SDK 模块独立相同 | `tests/phase4.py` |
| `build/grub-import-gcc.log`、`build/grub-import-phase0.log` | GCC x86 实际启动和 Phase 0 基线 | `tests/gcc_smoke.py`、`tests/phase0.py` |

FAT、ISO 没有通用 sparse-file 标志，以全零内容和碎片/multi-extent 布局校验读取；
ext/NTFS 的超 4 GiB 样本由 debugfs/ntfstruncate 生成，核对逻辑长度和选定区段，不声称完整搬运了 5 GiB 数据。
512/2048/4096 logical blocks 均有 host 证据；EFI 还实际使用 2048 和 4096 Block I/O 设备。
firmware rescan 在全部 target 实际执行；MediaId 更换/空介质/失败枚举注入属于 host EFI mock，
没有把 mock 报告成真实硬件热插拔。真实硬件、Secure Boot 和 OS handoff 均未验证。

Clang 的 function-type sanitizer 仅对六个 vendor 编译输入关闭：上游 fshelp 使用各 driver
不同 struct tag 的 opaque-node callback ABI。ASan 和其余 UBSan 检查仍启用，兼容层不豁免。
GCC 仅对 ntfscomp 的 maybe-uninitialized 诊断作局部豁免：其 error-slot 间接访问使编译器
无法推导 decomp_get16 的错误返回不读取输出。详见 cmake/storage.cmake。
