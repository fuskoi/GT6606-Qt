# GT6606 — Doppler 完整移植（Qt 全屏）

原 `Doppler` 工程**不修改**。业务源码已拷贝到本目录 `doppler_bus/`，板级适配与界面在本目录完成。

---

## 怎么做成的（结构）

```text
┌─────────────────────────────────────────────┐
│  同一个进程：doppler_gt6606                 │
│                                             │
│  Qt 界面 (doppler_ui/)                      │
│    · 先 showFullScreen → 画到屏幕           │
│    · 再启动业务                             │
│    · 定时读 Sensor 快照刷新标签             │
│    · 按钮调用 ReadFlowMeterData / 上报      │
│              ↓ 调用                         │
│  板级适配 (doppler/)                        │
│    · Sysinit.c：ttyAMA / UartDev / 看门狗   │
│    · DopplerApp.c：原 main 的 Init+定时器   │
│              ↓ 编译链接                     │
│  业务源码副本 (doppler_bus/)                │
│    · Include / Source / libota              │
│    · 无 Sysinit.c、无 main.c（已替换）      │
└─────────────────────────────────────────────┘
        ↓ Qt 平台插件 eglfs 或 linuxfb
   /dev/fb0 → 液晶屏
        ↓ 业务线程
   /dev/ttyAMA* 串口、Socket 上报
```

### 显示是怎么上去的

1. 板上运行 `run_doppler.sh`，设置：
   - `LD_LIBRARY_PATH` → Qt 动态库  
   - `QT_QPA_PLATFORM=eglfs`（不行再试 `linuxfb`）  
2. 程序里 `QApplication` + `showFullScreen()`  
3. Qt 把窗口像素写到显示设备，**不走串口屏协议**

### 业务是怎么跑的

- 原 Doppler 的定时器线程、串口线程、Socket 线程逻辑都在  
- 入口从原 `main` 的 `while(1)` 换成：`Doppler_BusinessStart()` + Qt `app.exec()`  
- 配置文件：可执行文件同目录的 `doppler.conf`

---

## 目录

| 路径 | 说明 |
|------|------|
| `doppler_bus/` | 从原 Doppler 拷来的业务代码 |
| `doppler/Sysinit.c` | GT6606 串口/看门狗适配 |
| `doppler/DopplerApp.c` | 业务启动 |
| `doppler/doppler.conf` | 默认模拟传感器、关看门狗 |
| `doppler_ui/` | Qt 监控界面 |
| `doppler_gt6606.pro` | qmake 工程 |

---

## 编译 / 部署 / 运行

虚拟机：

```bash
bash /mnt/win/Projects/QtProjects/GT6606/build_doppler_arm.sh
BOARD_IP=192.168.101.230 bash /mnt/win/Projects/QtProjects/GT6606/deploy_doppler.sh
```

板上（必须用脚本才能出界面）：

```bash
cd /home/htnice/Qtdemo
./run_doppler.sh
```

若黑屏：

```bash
QT_QPA_PLATFORM=linuxfb ./run_doppler.sh
```

---

## 配置要点（doppler.conf）

- `SensorSimulate=1`：无流量计也能出数  
- `WatchdogEnable=0`：避免误开看门狗导致重启  
- 串口默认 COM1..6 → `/dev/ttyAMA0..5`，可用 `UartDev5=/dev/ttyUSB0` 覆盖  
