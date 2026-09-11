# Phase 5：loopback 与 diskfilter

本次完成文件映像、LVM2 和 MD 1.x 的只读接入。上游文件保持原样，接口适配集中于
`core/grub/`；不包含 cryptodisk、透明解压、写入、设备导出或 OS handoff。

## 复现验证

在 README 所述 Ubuntu/WSL 环境，运行：

```sh
python3 tools/check_references.py
python3 tools/import_grub.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase5.py
python3 tests/storage.py
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase0.py
python3 tests/gcc_smoke.py
git diff --check
```

`phase5.py` 包含 Phase 4 的完整构建、不同源码路径重建、SDK 和固件回归；
随后调用 `volumes.py` 并执行新增固件场景。仅在已有**当前源码**的完整 Phase 4 结果时，
可以用 `python3 tests/phase5.py --skip-baseline` 定向重跑。所有 runtime 由 CMake 构建。

host 内容检验器的参数为 `BLOCK_SIZE VOLUME PATH IMAGE...`，输出文件原始字节；例如：

```sh
python3 tests/volumes.py
build/volumes/host/boot-volume-test 4096 lvm/fixture-data /probe.bin \
  build/volumes/fixtures/lvm.img > build/volumes/probe.bin
sha256sum build/volumes/probe.bin
```

`boot-volume-test` 是测试工具，包含生命周期断言；不是最终用户 CLI。

## 使用公开接口

入口在 `include/boot/volume.h`，文件和分区接口仍来自 `include/boot/storage.h`。

| 操作 | 调用约定 | 结果与错误 |
| --- | --- | --- |
| `boot_loopback_add(name, file, out)` | 先打开普通文件；名称以 `loop` 开头、少于 128 字节；来源必须已存在 | 复制 file/fs 句柄，新增只读 512-byte provider；重复名称、目录、空文件、溢出和过深来源拒绝 |
| `boot_diskfilter_scan(storage)` | 添加固件/host providers 和所需 loopback 后调用 | 扫描可读根设备、分区、loopback 和嵌套组合卷；不匹配或不可读候选被跳过，OOM/stale 显式返回 |
| `boot_volume_list(storage, hook, data)` | 扫描后枚举；回调可打开列出的卷 | 回调中不能 scan、add、reset；回调状态向上传递 |
| `boot_volume_open(storage, name, out)` | 使用枚举名或上游 `lvmid/`、`mduuid/` 名称 | 返回可供分区、文件系统和 byte-range read 使用的 slice；不存在返回 `BOOT_E_NOT_FOUND` |
| `boot_slice_physical(slice, out)` | 校验 generation 后查询 | loopback/组合卷及其子 slice 返回 false；物理 slice 返回 true，但不生成 extent/blocklist |
| `boot_volume_reset(storage)` | 结束会话；销毁 storage 前必须调用 | 先使所有 handles 失效，再释放 diskfilter 和 loopback 对象；物理 providers 也须重新枚举 |

例如，已有成功挂载的 `struct boot_fs fs` 时，先用 `boot_file_open(&fs, "/inner.img", &file)`
取得来源，再调用 `boot_loopback_add("loop0", &file, &slice)`。后续对 `slice` 使用
`boot_partitions()` 或 `boot_fs_mount()`，无需新写文件系统读取逻辑。

会话仅支持一个 `boot_storage`，符合当前单 CPU、单线程轮询 runtime。来源 provider 必须活到
reset，file/fs 调用方对象可在 add 成功后释放或重用。普通 `boot_storage_rescan()` 立即使
旧句柄 stale；下一次 volume API 会清理旧会话。释放 storage 本身时应显式 reset。

## 上游代码与资源边界

快照、提交和 SHA-256 见 `vendor/grub/sources.json` 和 `CODE_ORIGINS.md`。
新增原样文件为 `loopback.c`、`kern/list.c`、`list.h`、`lvm.h`；启用已有
diskfilter、LVM、MD 1.x 以及 RAID5/6 recovery。没有导入旧 GRUB command parser，
loopback 原命令函数只由带类型的 boot API 调用；cryptocheck 注册为不可调用占位。

分配限额仍为 4 MiB，runtime arena 为 5 MiB。扫描生成的上游状态归 session 所有，
普通读取 scratch 归当前 operation；reset 清理尚未显式释放的扫描分配。
原始 GRUB errno 仅映射到当前 operation 的私有槽，公开调用显式返回 `boot_status_t`。
已由上游成功完成的镜像/校验恢复不会被早先失败的成员读取覆盖为错误。

最多 32 个已注册块设备（物理与虚拟合计）、160 个扫描输入别名、32 个 loopback 名称。
loopback 来源 depth 小于 8；GRUB 虚拟调用和 LV 验证各限制为 16 层。
新增设备只能引用已有来源，不提供名称替换或单独 detach；块层用逐 slot active mask
拒绝自递归，并用独立对齐 scratch 防止嵌套读取覆盖当前缓存行。

本次新增一项上游补丁 `0003-diskfilter-lv-cycle.patch`：两个 LV 相互引用会令原
`validate_lv()` 无限递归。动态 `lvm-cycle.img` 在补丁前触发 ASan stack-overflow；
补丁后在校验阶段有界拒绝。vendor 快照未改变；补丁只在 build 目录应用。

物理可追溯性采用保守策略：即使单 PV 线性 LVM 或连续文件理论上可物理化，本阶段也返回
false。当前不生成跨 firmware handoff 可存活的 blocklist，后续 map 必须物化或明确拒绝。
虚拟别名不是稳定硬件 identity；底层 provider generation/identity 仍保留在来源 slice 中。
组合卷会保守校验本会话全部物理输入，因此无关输入 stale 也可能使其读取失效。

## 本次媒体覆盖

| 层次 | 已验证 | 未由本次证明 |
| --- | --- | --- |
| loopback | 双层 FAT 文件映像、调用方句柄重用、末扇区补零、重复名、非对齐/缓存碰撞、来源 stale | 透明解压、可写设备、任意深度 |
| LVM2 | 单 PV 两个非连续 segment、LVM-on-MD、loopback 内 MBR 分区上的 LVM | thin/cache/snapshot、所有多 PV/镜像布局、journal/一致性恢复 |
| MD 1.x | metadata 1.0/1.1/1.2；RAID0 两盘、RAID1 完整/缺一盘、左非对称 RAID5 完整/缺一盘 | RAID4/6/10 媒体、所有 layout、重建/reshape、事件计数一致性校验 |
| blocks | host 512/2048/4096；五个 firmware target，EFI 卷夹具使用 4096-byte Block I/O | 真实硬件及热插拔 |
| 拒绝 | MD 盘数、role、偏移溢出、版本；LVM label offset、metadata offset/size 溢出、双 LV 环 | 对任意恶意 LVM/MD 元数据的完整安全保证 |

MD/LVM 元数据由公开格式布局生成并包含 checksum 字段，FAT 内容由 mkfs.fat/mtools 创建；
本次不是从生产阵列采集的真实媒体验收。上游 reader 的 checksum/事件处理策略未扩展。
`mdraid_linux{,_be}.c`、`dmraid_nvidia.c`、`ldm.c` 仍仅预留、不编译。

完整证据在 `build/phase5/results.json`、串口日志，以及 `build/volumes/results.json`
和具名拒绝日志。持久摘要见 `docs/phase5-evidence.json`；Phase 4 回归原始结果仍在
`build/phase4/`。CI 已接入相同测试，不能据此推定远端 CI 已运行。
