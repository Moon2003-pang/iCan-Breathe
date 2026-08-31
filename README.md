# iCan-Breathe 🫁

**智能呼吸监测与预警系统** — 基于 ESP32-S3 + Qt 的实时呼吸监测终端

## 📋 项目简介

iCan-Breathe 是一个完整的智能呼吸监测解决方案，通过柔性传感器实时采集胸部呼吸运动数据，利用边缘 AI 进行呼吸状态分析，并在检测到异常时及时预警。

### 系统架构

```
┌─────────────┐    TCP/WiFi    ┌─────────────────┐
│  ESP32-S3   │ ────────────── │   Qt 上位机      │
│  (传感器端)  │                │  (监测终端)       │
│             │                │                  │
│ • 柔性传感器 │                │ • 实时数据可视化  │
│ • ADC 采集  │                │ • 呼吸状态分析    │
│ • 边缘 AI   │                │ • 异常预警        │
│ • 报警判断  │                │ • 健康报告生成    │
└─────────────┘                └─────────────────┘
```

## ✨ 核心功能

### 📊 实时监测
- **多通道数据采集**: ADC、电压、电阻、压力力值
- **实时波形显示**: 力值和电阻双通道实时图表
- **自适应基线校准**: 自动零点校准，跟踪传感器漂移

### 🌬️ 呼吸分析
- **呼吸状态识别**: 正常/异常状态实时判断
- **呼吸频率计算**: 自动计算每分钟呼吸次数
- **异常评分**: 基于重建误差的异常分数

### ⚠️ 智能预警
- **多级报警**: 按压力异常报警 + 呼吸异常报警
- **远程阈值配置**: 通过上位机远程调整报警阈值
- **紧急联系人**: 自动通知预设的紧急联系人
- **二次提醒**: 报警 30 秒未处理自动加强提醒
- **连接看门狗**: 设备离线自动检测与告警

### 📈 健康报告
- **本地评分**: 基于呼吸数据计算健康评分
- **AI 增强**: 集成大语言模型生成智能健康建议
- **历史数据**: CSV 格式存储呼吸历史记录
- **报告导出**: 支持 HTML 格式导出健康报告

### 📱 多平台支持
- **桌面端**: Windows / Linux / macOS
- **移动端**: Android 原生支持
- **自适应 UI**: 根据平台自动调整界面布局

## 🛠️ 技术栈

| 组件 | 技术 |
|------|------|
| **嵌入式端** | ESP32-S3, Arduino, TensorFlow Lite Micro |
| **上位机** | Qt 6, C++17, QCustomPlot |
| **通信协议** | TCP Socket, JSON |
| **数据存储** | CSV, QSettings |
| **AI 集成** | 大语言模型 API (可选) |

## 📁 项目结构

```
zhongyanBisai/
├── 04_Terminal/              # Qt 上位机主程序
│   ├── main.cpp              # 程序入口
│   ├── widget.h/cpp          # 主窗口核心逻辑
│   ├── widget_ui.cpp         # UI 界面布局
│   ├── widget_comm.cpp       # 通信模块
│   ├── widget_alert.cpp      # 报警模块
│   ├── widget_data.cpp       # 数据处理模块
│   ├── realtimechart.h/cpp   # 实时图表组件
│   ├── healthreportdialog.*  # 健康报告对话框
│   ├── zhongyan.pro          # Qt 项目文件
│   └── android/              # Android 配置
├── old/                      # 硬件主程序
├── .gitignore                # Git 忽略规则
└── README.md                 # 项目说明文档
```

## 🚀 快速开始

### 环境要求

- **Qt**: 6.0 或更高版本
- **编译器**: MinGW (Windows) / GCC (Linux) / Clang (macOS)
- **Android**: Android Studio + NDK (如需编译 Android 版本)

### 编译步骤

#### 桌面版 (Windows/Linux/macOS)

```bash
# 1. 克隆仓库
git clone https://github.com/Moon2003-pang/iCan-Breathe.git
cd iCan-Breathe/04_Terminal

# 2. 使用 Qt Creator 打开项目
#    或者命令行编译:
qmake zhongyan.pro
make          # Linux/macOS
mingw32-make  # Windows (MinGW)

# 3. 运行程序
./zhongyan    # Linux/macOS
zhongyan.exe  # Windows
```

#### Android 版本

1. 使用 Qt Creator 打开 `04_Terminal/zhongyan.pro`
2. 配置 Android Kit (需要 Android SDK + NDK)
3. 选择 Android 目标设备，点击构建运行

### 硬件连接

1. 将 ESP32-S3 与柔性传感器连接
2. 烧录对应的固件程序
3. 确保 ESP32-S3 与电脑在同一局域网
4. 在上位机中配置监听端口 (默认 8080)
5. 点击"启动监听"等待设备连接

## 📖 使用说明

### 基本操作

1. **启动监听**: 点击"启动"按钮，开始监听 ESP32 连接
2. **查看数据**: 实时查看电阻、力值、电压等数据
3. **模拟模式**: 无硬件时可使用模拟数据进行测试
4. **生成报告**: 点击"健康报告"按钮生成呼吸分析报告

### 报警处理

- 当检测到异常按压或呼吸异常时，界面会显示红色警告
- 点击"解除报警"手动清除报警
- 报警超过 30 秒未处理会自动加强提醒
- 紧急联系人会在配置后自动接收通知

### 数据导出

- 呼吸历史数据自动保存为 CSV 格式
- 健康报告支持导出为 HTML 文件
- 报警日志记录在 `alert_history.csv`

## 🔧 配置说明

### 紧急联系人配置

在程序目录创建 `emergency_contacts.json` 文件：

```json
{
  "contacts": [
    {
      "name": "张三",
      "phone": "13800138000",
      "email": "zhangsan@example.com"
    },
    {
      "name": "李四",
      "phone": "13900139000",
      "email": "lisi@example.com"
    }
  ]
}
```

### 校准参数

- **参考电阻 (refMohm)**: 分压电路中的参考电阻值，默认 10 MΩ
- **供电电压 (vccMv)**: 传感器供电电压，默认 3300 mV
- **力值缩放 (scale)**: 电阻到力值的转换系数，默认 1.0

## 📊 数据格式

### 传感器数据 (JSON)

```json
{
  "timestamp": 1693000000000,
  "adc": 2048,
  "voltage": 1650.5,
  "resistance": 10000000,
  "force": 0.523,
  "breathStatus": "正常",
  "anomalyScore": 0.15,
  "alert": 0
}
```

### 呼吸历史 CSV

```csv
timestamp,adc,voltage_mv,resistance_ohm,force_n,breath_status,anomaly_score,alert
1693000000000,2048,1650.5,10000000,0.523,正常,0.15,0
```

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 📞 联系方式

- GitHub: [Moon2003-pang](https://github.com/Moon2003-pang)
- Email: 2932706122@qq.com

## 🙏 致谢

感谢所有为本项目提供帮助和支持的人！

---

**iCan-Breathe** - 让呼吸更安心 💨
