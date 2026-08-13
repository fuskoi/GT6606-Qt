# libota 移植指南

通用 **A/B 槽固件升级**（网页 **FIRMWARE_UPDATE=102**）。库负责会话/落盘/切槽；发帧、日志、停业务、改启动项由产品适配层完成。

参考实现：本仓库 `Source/OtaUpgrade.c`、`Include/OtaUpgrade.h`。

---

## 前提条件

新工程需满足：

1. **Linux 用户态**（有 `fopen` / `rename` / `chmod` / 文件系统）
2. 已能解析网页下行帧，并抽出 `**command` + `body` + `body_len`**
3. 已有能力发 **上行扩展帧**（把 `report_cmd` + body 打成 F2 发出去）
4. 启动方式可由应用改写（常见：`/etc/init.d/rcS` 末尾 `&` 拉起程序）
5. 下行缓冲 ≥ **约 2KB+ 帧头**（本工程约 4KB），否则 102 大包收不全

裸机 / 无 rcS 时仍可移植：把步骤 5 的 `activate_bin` 改成写 bootloader 槽位标志即可。

---

## 步骤 1：拷贝库

把整个目录拷到新工程，例如：

```text
your_project/
  libota/
    include/ota.h
    include/ota_linux.h
    src/ota.c
    src/ota_linux_rcs.c
    README.md
```

**不要**改 `libota/src` 里的业务逻辑；产品差异全部写在适配文件里。

---

## 步骤 2：加入编译

Makefile 示例：

```makefile
LIBOTA_DIR = $(PWD)/libota
CFLAGS    += -I$(LIBOTA_DIR)/include
LIBOTA_SRCS = $(wildcard $(LIBOTA_DIR)/src/*.c)
LIBOTA_OBJS = $(patsubst $(LIBOTA_DIR)/src/%.c,$(OBJ_DIR)/libota_%.o,$(LIBOTA_SRCS))

$(OBJ_DIR)/libota_%.o: $(LIBOTA_DIR)/src/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# 最终链接加上 $(LIBOTA_OBJS)
```

CMake 示例：

```cmake
add_library(ota STATIC
  libota/src/ota.c
  libota/src/ota_linux_rcs.c)
target_include_directories(ota PUBLIC libota/include)
target_link_libraries(your_app PRIVATE ota)
```

---

## 步骤 3：新建产品适配文件

建议新建两个文件（可从 Doppler 的 `OtaUpgrade.*` 复制后改）：

- `ProductOta.h` — 对外 `Init` / `HandleDownlink` / `IsBusy`
- `ProductOta.c` — 实现 `ota_ops_t` 五个回调 + `ota_init`

### 3.1 必改配置（`ota_config_t`）


| 字段                     | 含义                 | 示例                    |
| ---------------------- | ------------------ | --------------------- |
| `root_dir`             | OTA 目录所在根（运行时工作目录） | `getcwd` 结果，盘根可用 `""` |
| `bin_name`             | 槽内可执行文件名           | `"YOUR_APP"`          |
| `slot_size_max`        | 单文件上限              | `2*1024*1024`         |
| `cmd_fw102`            | 下行命令字              | `102`（与网页一致）          |
| `rpt_reply`            | 102 ACK 上行命令字      | `120`                 |
| `rpt_result`           | 切槽结果上行命令字          | `13`                  |
| `require_elf`          | 是否校验 ELF 头         | Linux 程序填 `1`         |
| `fw102_ack_then_write` | 先 ACK 再写 Flash     | 慢 NAND 填 `1`          |


### 3.2 五个回调（`ota_ops_t`）


| 回调                | 必须？      | 做什么                              |
| ----------------- | -------- | -------------------------------- |
| `send`            | **必须**   | 把 `report_cmd` + body 打成你们的上行帧发出 |
| `activate_bin`    | **强烈建议** | 改 rcS / 启动脚本，让重启后跑新槽二进制          |
| `schedule_reboot` | **强烈建议** | 延时几秒后 `reboot`（先让 RESULT 发出去）    |
| `on_busy`         | 建议       | `busy=1` 时停抢 Flash/串口的进程与写盘      |
| `vlog`            | 建议       | 接到你们自己的日志                        |


`activate_bin` 改 rcS 可直接用库辅助：

```c
#include "ota_linux.h"

static int my_activate(void *user, const char *bin_path, uint8_t slot)
{
    static const char *const drops[] = {
        "YOUR_APP",   /* 旧进程名 / 旧路径关键字，命中则删该行 */
        "ota/slot_",
        NULL
    };
    (void)user;
    (void)slot;
    return ota_linux_rewrite_rcs("/etc/init.d/rcS", bin_path, drops);
}
```

`on_busy` 示例：

```c
static void my_on_busy(void *user, int busy)
{
    (void)user;
    if (busy) {
        system("killall OTHER_WRITER >/dev/null 2>&1");
        /* 可选：暂停本进程日志写 Flash、暂停传感器采集 */
    }
}
```

### 3.3 Init 骨架

```c
int ProductOta_Init(void)
{
    ota_config_t cfg;
    ota_ops_t ops;
    char cwd[260];

    ota_config_init_defaults(&cfg);
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return -1;
    strncpy(cfg.root_dir, cwd, sizeof(cfg.root_dir) - 1);

    cfg.bin_name = "YOUR_APP";
    cfg.slot_size_max = 2 * 1024 * 1024;
    cfg.cmd_fw102 = 102;
    cfg.rpt_reply = 120;
    cfg.rpt_result = 13;
    cfg.require_elf = 1;
    cfg.fw102_ack_then_write = 1;

    memset(&ops, 0, sizeof(ops));
    ops.send = my_send;
    ops.vlog = my_vlog;
    ops.on_busy = my_on_busy;
    ops.schedule_reboot = my_reboot;
    ops.activate_bin = my_activate;

    return ota_init(&cfg, &ops);
}
```

---

## 步骤 4：开机初始化

在 `main`（或系统 init 完成、UART/协议栈就绪之后）调用：

```c
ProductOta_Init();
```

`ota_init` 会创建：

```text
{root_dir}/ota/
  active          # 首次写成 A
  slot_a/
  slot_b/
```

---

## 步骤 5：挂接下行

在「已解出扩展下行命令字 + 业务体」的地方：

```c
if (ota_handle_downlink(cmd, body, body_len) == OTA_OK) {
    /* 是 102，已被处理（含 ACK） */
} else {
    /* 其它命令走原有业务 */
}
```

注意：

- `cmd` 必须等于配置的 `cmd_fw102`（默认 102）
- `body` 是 **102 业务体**，不要带外层 SL651/F1 头
- 乱序包库会回 **lastOk** ACK；不要自己再补发错误序号

---

## 步骤 6：升级中让出 Flash / 串口

凡是会 **写 Flash、占 DTU 串口、打大量日志** 的定时任务，入口加：

```c
if (ota_is_busy())
    return;
```

至少覆盖：

- 周期网页/遥测上报
- 传感器读/断面落盘
- 其它同机写根分区的进程（在 `on_busy` 里 kill）

下行解析里升级大包建议 **少打 hex 日志**，否则 YAFFS 上容易拖垮收包。

---

## 步骤 7：确认启动链路

升级成功后库会：

1. 把 `staging.bin` rename 成 `{root}/ota/slot_x/YOUR_APP`
2. 写 `ota/active` = `A`/`B`
3. 调 `activate_bin`（改 rcS 末行：`/path/to/ota/slot_x/YOUR_APP &`）
4. `schedule_reboot` → 重启

请人工检查设备上的 `rcS`（或等价脚本）：

- 旧启动行已被删掉（靠 `drops[]` 关键字匹配）
- 新路径正确且 **可执行**（`chmod 0755`）
- 工作目录若影响相对路径配置，与 `root_dir` 一致

---

## 步骤 8：联调检查清单

- 网页下发 102，设备每包回 120 ACK（`replyCmd=102`，pkt 大端）
- 首包非 `7F ELF` 时拒绝落地、不改坏 rcS
- 收齐后有 RESULT(13)，约 5s 内 reboot
- 重启后跑的是 **新槽** 路径（`ps` / `ls -l /ota/…`）
- 升级过程中无其它进程狂写同分区 Flash
- 中途断线再开：pkt=1 能重新开会话

---

## 协议摘要（给网页/后台）

下行 body：

```text
type(u8) + pktIndex(u16 BE) + pktTotal(u16 BE) + fileSize(u32 BE) + payload
```

- `pktIndex` 从 **1** 开始  
- 乱序时设备 ACK 的是 **已确认最后一包 lastOk**，不是 expect

上行 ACK（命令字默认 120）：

```text
replyCmd(102) + type + pktIndex(u16 BE)
```

---

## 打包校验 + 启动回滚（Doppler）

网页协议不变，仍传一个文件。发布前在 PC 上打包：

```bash
python libota/tools/ota_pack.py DOPPLER.1.0 -c -o DOPPLER.1.0.ota
```

生成带 48 字节尾部的包：`DPLRPKG1` + 产品 ID(`DPLR`) + ELF 长度 + SHA256。  
设备收齐后校验，剥尾再激活；裸 ELF / 别的产品包会被拒绝。

启动回滚：

1. 切槽前写 `{root}/ota/boot_pending`（旧槽路径）
2. `rcS` 改为跑 `{root}/ota/ota_boot.sh`
3. 新程序 `main` 里调用 `OtaUpgrade_ConfirmBoot()` 清 pending  
4. 若新程序起不来再次开机，脚本把 active 切回旧槽并启动旧程序

---

## 常见坑


| 现象           | 排查                                                   |
| ------------ | ---------------------------------------------------- |
| 完全无 ACK      | `send` 未接到串口 / 命令字不是 120                             |
| 只到前几包就停      | 缓冲太小、写 Flash 阻塞且未开 `fw102_ack_then_write`、其它进程抢 NAND |
| 重启后还是旧程序     | `activate_bin`/`rcS` 未改对，或 `drops` 没删掉旧行             |
| 一升级就砖        | 假包非 ELF；确认 `require_elf=1`                           |
| `root_dir` 错 | `getcwd` 与真实安装目录不一致，槽建在别处                            |


---

## 目录对照


| Doppler                             | 新项目建议               |
| ----------------------------------- | ------------------- |
| `libota/`                           | 原样拷贝                |
| `Source/OtaUpgrade.c`               | `ProductOta.c`（改回调） |
| `Include/OtaUpgrade.h`              | `ProductOta.h`      |
| `Command_proc.c` 里 `HandleDownlink` | 你们的下行分发处            |
| `main` 里 `OtaUpgrade_Init`          | `ProductOta_Init`   |
| 各处 `OtaUpgrade_IsBusy`              | `ota_is_busy()`     |


