# GT6606 — TEL-15 渠道测流（Qt 全屏）

原 `TEL-15-渠道测流` **不修改**。业务源码拷在 `tel15_bus/`，板级适配与界面在本目录完成。设备与 `GT6606_tes91`、`GT6606_time-difference` 相同：GT6606（1024×600，`/dev/ttyAMA*`，linuxfb）。

## 结构

| 路径 | 说明 |
|------|------|
| `tel15_bus/` | 从原 TEL-15 `code/` 拷来的 Include / Source / libota |
| `tel15/Sysinit.c` | GT6606：默认 `/dev/ttyAMA0..5`，缺口不退出，看门狗默认关 |
| `tel15/Tel15App.c` | 原 `main` 的初始化与定时器（无 `while(1)`） |
| `tel15/tyls.conf` | 默认配置（WebLinkMode=1，WatchdogEnable=0） |
| `tel15_ui/` | Qt 全屏监测界面，替代原 ZigBee 触摸屏推送 |
| `tel15_gt6606.pro` | qmake 工程 |

## 编译 / 部署 / 运行

虚拟机：

```bash
bash /mnt/win/Projects/QtProjects/GT6606_tel15/build_tel15_arm.sh
BOARD_IP=192.168.101.230 bash /mnt/win/Projects/QtProjects/GT6606_tel15/deploy_tel15.sh
```

板上：

```bash
cd /work/tel15
./run_tel15.sh
```

黑屏时：

```bash
QT_QPA_PLATFORM=linuxfb ./run_tel15.sh
```

## 配置要点（tyls.conf）

- 串口默认 COM1..6 → `/dev/ttyAMA0..5`，可用 `UartDev5=/dev/ttyUSB0` 覆盖
- `WatchdogEnable=0`：避免误开看门狗重启
- `WebLinkMode=1`：Socket 上报；`PreferUplink=auto` 由启动脚本选 eth0 / usb0
- Qt 界面读业务全局量刷新；`BOARD_GT6606` 下不再向触摸屏口推屏数据
