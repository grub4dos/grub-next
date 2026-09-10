# Phase 3：自包含资源

五个 target 使用同一只读 `cpio newc` reader，从 archive 取得模块、默认 Lua 源文件和
最小诊断字体。模块经过已有 ELF/ABI 验证后实际执行。Lua 执行属于 Phase 6，字体渲染属于
Phase 13；内容清单用于检测损坏，不提供签名认证。

## 构建与启动

在 Ubuntu/WSL 安装 README 中的构建和模拟器依赖后运行：

```sh
SOURCE_DATE_EPOCH=1704067200 python3 tests/phase3.py
```

每个构建目录新增 `resource.cpio`、`resource.manifest.sha256`。CMake 先构建树外 SDK 样本，
再调用 `tools/pack_resources.py`，最后将同一 archive 字节放入映像的 `.bootres` section。
EFI 四个架构的 PE section 均为只读、不可执行；LoongArch 的构建期 ELF→PE 转换显式保留
该 section，运行时仍由固件加载整个 EFI 映像。

BIOS 可使用以下 GRUB2 配置（先将对应构建产物复制到 `/boot/`）：

```text
multiboot2 /boot/boot-core.elf
module2 /boot/resource.cpio
boot
```

Linux 协议对应使用：

```text
linux /boot/boot-linux.bz
initrd /boot/resource.cpio
boot
```

没有外部 archive 时使用映像内嵌资源。外部输入损坏时返回错误，不回退绕过错误。
测试另使用 SeaBIOS `-kernel` / `-initrd`，不连接磁盘或 CD，证明 stage2 的资源消费
不依赖可识别磁盘文件系统；初始加载由上级 loader/固件完成。

## 输入和生命周期

`boot_resource_open(context, archive)` 在 BIOS context 输入内存保留及 physical/bounce
初始化后调用。Multiboot2 使用第一个 module；Linux 32 位入口使用 `ramdisk_image` 和
`ramdisk_size`，没有猜测高地址扩展字段。BIOS 从输入物理区域通过已有有界 physical API
复制到低地址 BL 分配，再解析和校验；失败释放该副本。成功副本保留到 handoff。
EFI 和无外部输入的 BIOS 直接借用内嵌 section，生命周期随 core image。

`boot_archive_open` 完整验证 archive 后才公开索引；失败时 `count=0`。
`boot_archive_find` 返回只读文件视图，不分配或复制文件内容；调用方必须保持底层内存
存活且不可变。`boot_archive_verify` 验证每个普通文件与 manifest 一致，模块执行必须
在此步骤成功以后。后续 storage/file API 可以复用该 reader，当前没有引入 disk provider。

| 接口 | 成功结果 | 失败 |
| --- | --- | --- |
| `boot_archive_open` | `BOOT_OK`，完整索引 | 格式/范围错误 `BOOT_E_INVALID`；超过条目数 `BOOT_E_NOMEM` |
| `boot_archive_find` | `BOOT_OK`，name/data/size/mode 视图 | 无文件、目录或非法参数 `BOOT_E_INVALID` |
| `boot_archive_verify` | `BOOT_OK`，全部普通文件匹配 | 缺失、损坏或不一致的清单 `BOOT_E_INVALID` |
| `boot_resource_open` | `BOOT_OK`，已校验资源 | 传播 reader、物理读取、BL 分配错误 |

## 格式约束

支持 `070701`、110 字节 header、严格八位十六进制字段、4 字节对齐、必需的
`TRAILER!!!` 和尾部零填充。允许普通文件和空目录，拒绝 CRC newc `070702`、旧 cpio、
符号链接、普通文件硬链接、设备节点、重复名称和拼接 archive。上限为 16 MiB、128 条
目录/文件记录、255 字节路径。路径使用 ASCII 字母、数字、`_`、`-`、`.` 和分隔符 `/`；
禁止绝对路径、空分量、`.`/`..`、反斜杠和嵌入 NUL。资源内容可以是任意字节，Lua 源文件为 UTF-8。

打包器只接受显式 `--file NAME=PATH` 映射，不扫描目录、不保留 host metadata。
文件按路径排序，inode 从 1 顺序分配，mode 固定 `0100644`，uid/gid/mtime/device/check
全部为零，nlink 为 1，尾部对齐到 512 字节。因此不受来源路径、文件 mtime、参数顺序或
`SOURCE_DATE_EPOCH` 值影响。相同输入内容生成相同 archive 和 manifest。

清单名称是 `manifest.sha256`，每行是小写 SHA-256、两个空格、路径和 LF。每个普通文件
恰好一行，顺序与 archive 一致；不包含清单自身，reader 不接受遗漏或多余行。它没有签名，
不能防止攻击者同时替换文件及其摘要。目录 metadata 不属于内容摘要。

默认资源包括两个 target 对应 SDK 模块、`boot.lua`、`fonts/minimal.hex` 和清单。
字体是本项目绘制的 5×7 文本 glyph 数据，含空格、问号、数字和大写拉丁字母，未知字符
使用问号；不是完整 ASCII/CJK 字库，也没有在本阶段接入渲染器。

## 验证与证据

`tests/phase3.py` 复用 Phase 1/2 的启动、模块事务和树外 SDK 验收，增加三种资源输入、
无磁盘内嵌回退、损坏 header/内容拒绝、四种 PE section 字节比对、跨 target 逻辑资源
一致性。不同源码路径独立构建同时比较 runtime、archive 和 manifest。

Host CTest 的 `resource-archive` 使用 SHA-256 已知答案、每个截断位置和 10,000 次变异；
`resource-packer` 用独立 Python newc parser 和 hashlib 检查格式及 SHA padding 边界，
并测试路径、hex、长度溢出、链接、重复、条目上限、缺清单和多余尾部。
`resource-input` 注入空输入、超大长度、OOM、I/O 和内容损坏，核对失败清理与首个 module
选择。安装 GNU cpio 时还双向验证公开工具互操作；打包器命令行测试改变来源路径、mtime、
参数顺序和 epoch，比较 archive、生成头和清单。
可通过 README 中 `BOOT_SANITIZERS=ON` 命令复现 ASan/UBSan。

原始命令、串口、固件/产物 SHA-256 和 results 位于 `build/phase3/`，持久摘要见
[phase3-evidence.json](phase3-evidence.json)，实际执行结果与边界见 [progress.md](../progress.md)。
这些证据证明 QEMU 上资源读取和模块执行；不代表真实硬件、Secure Boot 或 OS handoff 验收。

## 来源

迁移前执行 `python3 tools/check_references.py`。参考锁定 GNU GRUB 提交
`2f972128c48b90bf8b63aadffe6d546976e1dee6` 的 `grub-core/fs/newc.c` 和
`cpio_common.c`，核对 header、alignment 和 trailer。reader 为新写实现，没有引入
`grub_errno`、disk、archelp、注册/卸载或 libc 依赖。详细来源见
[CODE_ORIGINS.md](../CODE_ORIGINS.md)。
