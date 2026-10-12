# TES91_GT6606 — 通辽泥沙升降（Qt 全屏）

原 `TES-91/code/code-tongliao` **不修改**。业务源码已拷贝到本目录 `nisha_bus/`，板级适配与界面在本目录完成。结构对齐 `QtProjects/GT6606`（Doppler）。

---

## 结构

```text
┌─────────────────────────────────────────────┐
│  同一个进程：nisha_gt6606                   │
│                                             │
│  Qt 界面 (nisha_ui/)                        │
│    · 全屏显示深度 / 泥沙 / 设备状态         │
│    · 默认定深 / 垂线混合 / 垂线积深 / 手动  │
│    · 替代原 LoRa 串口屏协议                 │
│              ↓ 调用                         │
│  板级入口 (nisha/NishaApp.c)                │
│    · 原 main：读 conf、开串口、任务线程     │
│              ↓ 编译链接                     │
│  业务源码副本 (nisha_bus/)                  │
│    · Include / Source / libota              │
│    · BOARD_GT6606：ttyAMA + 串口软失败      │
└─────────────────────────────────────────────┘
        ↓ QT_QPA_PLATFORM=linuxfb|eglfs
   /dev/fb0 → 液晶屏
        ↓ 业务线程
   /dev/ttyAMA* 串口；上报走 Socket（usb0/eth0）或可选 DTU 串口
```

---

## 目录

| 路径 | 说明 |
|------|------|
| `nisha_bus/` | 从 code-tongliao 拷来的业务代码 |
| `nisha/NishaApp.c` | 业务启动（替代原 main） |
| `nisha/tyls.conf` | GT6606 默认配置（无 LoRa 屏） |
| `nisha_ui/` | Qt 监控与操作界面 |
| `nisha_gt6606.pro` | qmake 工程 |

---

## 配置要点（tyls.conf）

- 串口：COM1..6 → `/dev/ttyAMA0..5`（本项目默认只用 COM1–4）
- **未配置** `LORA_SCREEN`（改用 Qt 屏）；`UART1` 作电机口
- `4G_DTU=2`（仅 `WebLinkMode=0`），`SEDIMENT/DEPTH=3`（共口），`DC_MOTOR=1`，`RELAY_MODULE=4`
- **上报与多普勒同板**：`WebLinkMode=1` + `SocketServerIP/Port`，经板载 4G（`usb0`）或网线（`eth0`）TCP；`run_nisha.sh` 按 `PreferUplink` 选路
- `DOG=0`：默认关看门狗，现场确认后再开

接线与原通辽板角色一致时，只需确认 GT6606 物理串口对应关系。

---

## 编译 / 部署 / 运行

虚拟机：

```bash
bash /mnt/win/Projects/QtProjects/TES91_GT6606/build_nisha_arm.sh
BOARD_IP=192.168.101.230 bash /mnt/win/Projects/QtProjects/TES91_GT6606/deploy_nisha.sh
```

板上：

```bash
cd /work/nisha
./run_nisha.sh
```

若黑屏：

```bash
QT_QPA_PLATFORM=linuxfb ./run_nisha.sh
# 或
QT_QPA_PLATFORM=eglfs ./run_nisha.sh
```
