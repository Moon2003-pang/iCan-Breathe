// widget_ui.cpp — UI 构建 & QSS 样式
// 从 widget.cpp 拆分：setupUi, setupTopBar/Content/BottomBar, 移动端布局, 工厂函数, applyStyles

#include "widget.h"
#include "realtimechart.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QScrollerProperties>
#include <QEasingCurve>
#include <QScreen>
#include <QSizePolicy>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

// =====================================================================
// 根布局入口
// =====================================================================

void Widget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    setupTopBar();
    setupContent();
    setupBottomBar();
}

// =====================================================================
// 顶部栏 — PC / 移动端
// =====================================================================

void Widget::setupTopBar()
{
#ifdef Q_OS_ANDROID
    setupMobileTopBar();
    return;
#endif

    auto *topBar = new QFrame(this);
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(58);

    auto *layout = new QHBoxLayout(topBar);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(12);

    auto *title = new QLabel(QString::fromUtf8("电阻-力监测上位机"), topBar);
    title->setObjectName("titleLabel");
    layout->addWidget(title);

    auto *subtitle = new QLabel(QString::fromUtf8("ESP32-S3 数据采集 / 电阻换算 / 力波形"), topBar);
    subtitle->setObjectName("subtitleLabel");
    layout->addWidget(subtitle);

    layout->addStretch(1);

    m_statusDot = new QLabel(topBar);
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setFixedSize(12, 12);
    layout->addWidget(m_statusDot);

    m_statusText = new QLabel(QString::fromUtf8("未监听"), topBar);
    m_statusText->setObjectName("statusText");
    layout->addWidget(m_statusText);

    auto *portLabel = new QLabel(QString::fromUtf8("端口"), topBar);
    portLabel->setObjectName("inlineLabel");
    layout->addWidget(portLabel);

    m_portEdit = new QLineEdit("8080", topBar);
    m_portEdit->setObjectName("compactEdit");
    m_portEdit->setAlignment(Qt::AlignCenter);
    m_portEdit->setFixedWidth(78);
    layout->addWidget(m_portEdit);

    m_startButton = new QPushButton(QString::fromUtf8("开始监听"), topBar);
    m_startButton->setObjectName("primaryButton");
    connect(m_startButton, &QPushButton::clicked, this, &Widget::startServer);
    layout->addWidget(m_startButton);

    m_stopButton = new QPushButton(QString::fromUtf8("停止"), topBar);
    m_stopButton->setObjectName("secondaryButton");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &Widget::stopServer);
    layout->addWidget(m_stopButton);

    m_reportButton = new QPushButton(QString::fromUtf8("生成报告"), topBar);
    m_reportButton->setObjectName("primaryButton");
    connect(m_reportButton, &QPushButton::clicked, this, &Widget::openHealthReport);
    layout->addWidget(m_reportButton);

    static_cast<QVBoxLayout *>(this->layout())->addWidget(topBar);
}

void Widget::setupMobileTopBar()
{
    auto *topBar = new QFrame(this);
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(64);

    auto *layout = new QHBoxLayout(topBar);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(10);

    auto *title = new QLabel(QString::fromUtf8("绣境知微"), topBar);
    title->setObjectName("titleLabel");
    layout->addWidget(title);

    layout->addStretch(1);

    m_statusDot = new QLabel(topBar);
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setFixedSize(12, 12);
    layout->addWidget(m_statusDot);

    m_statusText = new QLabel(QString::fromUtf8("未监听"), topBar);
    m_statusText->setObjectName("statusText");
    m_statusText->setVisible(false);
    layout->addWidget(m_statusText);

    m_reportButton = new QPushButton(QString::fromUtf8("报告"), topBar);
    m_reportButton->setObjectName("primaryButton");
    connect(m_reportButton, &QPushButton::clicked, this, &Widget::openHealthReport);
    layout->addWidget(m_reportButton);

    static_cast<QVBoxLayout *>(this->layout())->addWidget(topBar);
}

// =====================================================================
// 主内容区 — PC
// =====================================================================

void Widget::setupContent()
{
#ifdef Q_OS_ANDROID
    setupMobileContent();
    return;
#endif

    auto *content = new QWidget(this);
    content->setObjectName("content");
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(12);

    auto *left = new QWidget(content);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto *metricsPanel = createPanel(QString::fromUtf8("实时数据"));
    auto *metricsGrid = new QGridLayout();
    metricsGrid->setHorizontalSpacing(12);
    metricsGrid->setVerticalSpacing(10);

    m_resistanceValue = createMetricValue();
    m_forceValue = createMetricValue();
    m_voltageValue = createMetricValue();
    m_adcValue = createMetricValue();
    m_timestampValue = createMetricValue();
    m_dataSourceValue = createMetricValue(QString::fromUtf8("等待数据"));

    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("电阻")), 0, 0);
    metricsGrid->addWidget(m_resistanceValue, 1, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("力")), 0, 1);
    metricsGrid->addWidget(m_forceValue, 1, 1);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("电压")), 2, 0);
    metricsGrid->addWidget(m_voltageValue, 3, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("ADC")), 2, 1);
    metricsGrid->addWidget(m_adcValue, 3, 1);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("时间戳")), 4, 0);
    metricsGrid->addWidget(m_timestampValue, 5, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("数据来源")), 4, 1);
    metricsGrid->addWidget(m_dataSourceValue, 5, 1);

    auto *metricsInner = qobject_cast<QVBoxLayout *>(metricsPanel->layout());
    metricsInner->addLayout(metricsGrid);
    leftLayout->addWidget(metricsPanel);

    auto *forcePanel = createPanel(QString::fromUtf8("力值状态"));
    auto *forceInner = qobject_cast<QVBoxLayout *>(forcePanel->layout());
    m_forceBar = new QProgressBar(forcePanel);
    m_forceBar->setObjectName("forceBar");
    m_forceBar->setRange(0, 1000);
    m_forceBar->setTextVisible(false);
    m_forceBar->setFixedHeight(14);
    forceInner->addWidget(m_forceBar);
    m_baselineStatus = new QLabel(QString::fromUtf8("零点: 等待连接..."), forcePanel);
    m_baselineStatus->setObjectName("hintLabel");
    forceInner->addWidget(m_baselineStatus);

    m_breathStatusLabel = new QLabel(QString::fromUtf8("呼吸状态: 等待数据..."), forcePanel);
    m_breathStatusLabel->setObjectName("breathStatus");
    forceInner->addWidget(m_breathStatusLabel);

    m_calibrateButton = new QPushButton(QString::fromUtf8("手动校准零点"), forcePanel);
    m_calibrateButton->setObjectName("secondaryButton");
    connect(m_calibrateButton, &QPushButton::clicked, this, &Widget::triggerCalibrate);
    forceInner->addWidget(m_calibrateButton);

    auto *forceHint = new QLabel(QString::fromUtf8("力 = 二次模型 (3点标定)，零点自动标定。进度条按力值绝对值显示。"), forcePanel);
    forceHint->setObjectName("hintLabel");
    forceHint->setWordWrap(true);
    forceInner->addWidget(forceHint);
    leftLayout->addWidget(forcePanel);

    auto *calPanel = createPanel(QString::fromUtf8("换算参数"));
    auto *calGrid = new QGridLayout();
    calGrid->setHorizontalSpacing(8);
    calGrid->setVerticalSpacing(8);

    m_refEdit = createNumberEdit("10.0");
    m_vccEdit = createNumberEdit("3300");
    m_zeroEdit = createNumberEdit("--");
    m_zeroEdit->setReadOnly(true);
    m_zeroEdit->setToolTip(QString::fromUtf8("自动标定：连接后 10 秒采集的电阻平均值"));
    m_scaleEdit = createNumberEdit("1.0");

    calGrid->addWidget(new QLabel(QString::fromUtf8("参考电阻 MΩ")), 0, 0);
    calGrid->addWidget(m_refEdit, 1, 0);
    calGrid->addWidget(new QLabel(QString::fromUtf8("供电 mV")), 2, 0);
    calGrid->addWidget(m_vccEdit, 3, 0);
    calGrid->addWidget(new QLabel(QString::fromUtf8("零点 MΩ")), 4, 0);
    calGrid->addWidget(m_zeroEdit, 5, 0);
    calGrid->addWidget(new QLabel(QString::fromUtf8("力系数 N/MΩ")), 6, 0);
    calGrid->addWidget(m_scaleEdit, 7, 0);

    auto *calInner = qobject_cast<QVBoxLayout *>(calPanel->layout());
    calInner->addLayout(calGrid);
    auto *calHint = new QLabel(QString::fromUtf8("参考电阻、供电：手动设置。零点：连接后自动标定（10秒平均）。力 = 二次模型 (3点标定 F(R) = R0 - a·F + b·F²)。"), calPanel);
    calHint->setObjectName("hintLabel");
    calHint->setWordWrap(true);
    calInner->addWidget(calHint);
    leftLayout->addWidget(calPanel);

    auto *buttonPanel = createPanel(QString::fromUtf8("操作"));
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_clearButton = new QPushButton(QString::fromUtf8("清空数据"), buttonPanel);
    m_clearButton->setObjectName("secondaryButton");
    connect(m_clearButton, &QPushButton::clicked, this, &Widget::clearData);
    buttonLayout->addWidget(m_clearButton);

    m_simButton = new QPushButton(QString::fromUtf8("模拟数据"), buttonPanel);
    m_simButton->setObjectName("secondaryButton");
    connect(m_simButton, &QPushButton::clicked, this, [this]() {
        if (m_simTimer->isActive()) {
            m_simTimer->stop();
            m_simButton->setText(QString::fromUtf8("模拟数据"));
        } else {
            m_simTimer->start(simulationIntervalMs());
            m_simButton->setText(QString::fromUtf8("停止模拟"));
        }
    });
    buttonLayout->addWidget(m_simButton);
    buttonLayout->addStretch(1);

    auto *speedLayout = new QHBoxLayout();
    speedLayout->setContentsMargins(0, 0, 0, 0);

    auto *speedLabel = new QLabel(QString::fromUtf8("模拟速度"), buttonPanel);
    speedLabel->setObjectName("inlineLabel");
    speedLayout->addWidget(speedLabel);

    m_simSpeedSlider = new QSlider(Qt::Horizontal, buttonPanel);
    m_simSpeedSlider->setObjectName("speedSlider");
    m_simSpeedSlider->setRange(1, 10);
    speedLayout->addWidget(m_simSpeedSlider, 1);

    m_simSpeedText = new QLabel(QString::fromUtf8("4x"), buttonPanel);
    m_simSpeedText->setObjectName("speedText");
    m_simSpeedText->setMinimumWidth(30);
    m_simSpeedText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_simSpeedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_simSpeedText->setText(QString::fromUtf8("%1x").arg(value));
        m_settings->setValue("simulation/speed", value);
        if (m_simTimer->isActive()) {
            m_simTimer->start(simulationIntervalMs());
        }
    });
    speedLayout->addWidget(m_simSpeedText);

    auto *buttonInner = qobject_cast<QVBoxLayout *>(buttonPanel->layout());
    buttonInner->addLayout(buttonLayout);
    buttonInner->addLayout(speedLayout);
    leftLayout->addWidget(buttonPanel);
    leftLayout->addStretch(1);

    // --- Data collection panel (desktop) ---
    auto *collectPanel = createPanel(QString::fromUtf8("数据采集"));
    auto *collectInner = qobject_cast<QVBoxLayout *>(collectPanel->layout());

    auto *forceRow = new QHBoxLayout();
    forceRow->setContentsMargins(0, 0, 0, 0);
    auto *forceLabel = new QLabel(QString::fromUtf8("施加力 (N):"), collectPanel);
    forceLabel->setObjectName("inlineLabel");
    forceRow->addWidget(forceLabel);
    m_forceInput = new QLineEdit("0.0", collectPanel);
    m_forceInput->setObjectName("numberEdit");
    m_forceInput->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_forceInput->setFixedWidth(88);
    forceRow->addWidget(m_forceInput);
    forceRow->addStretch(1);
    collectInner->addLayout(forceRow);

    auto *collectBtnRow = new QHBoxLayout();
    collectBtnRow->setContentsMargins(0, 0, 0, 0);
    m_collectButton = new QPushButton(QString::fromUtf8("开始采集"), collectPanel);
    m_collectButton->setObjectName("secondaryButton");
    connect(m_collectButton, &QPushButton::clicked, this, &Widget::toggleCollect);
    collectBtnRow->addWidget(m_collectButton);
    m_saveButton = new QPushButton(QString::fromUtf8("保存数据"), collectPanel);
    m_saveButton->setObjectName("secondaryButton");
    connect(m_saveButton, &QPushButton::clicked, this, &Widget::saveCollectedData);
    collectBtnRow->addWidget(m_saveButton);
    collectBtnRow->addStretch(1);
    collectInner->addLayout(collectBtnRow);

    m_collectStatus = new QLabel(QString::fromUtf8("状态: 已采集 0 条"), collectPanel);
    m_collectStatus->setObjectName("hintLabel");
    collectInner->addWidget(m_collectStatus);

    m_breathModeCheck = new QCheckBox(QString::fromUtf8("呼吸模式（自动记录力值）"), collectPanel);
    m_breathModeCheck->setObjectName("hintLabel");
    collectInner->addWidget(m_breathModeCheck);

    leftLayout->addWidget(collectPanel);

    // --- Alert panel (Task-D) ---
    auto *alertPanel = createPanel(QString::fromUtf8("报警状态"));
    auto *alertInner = qobject_cast<QVBoxLayout *>(alertPanel->layout());

    m_alertLabel = new QLabel(QString::fromUtf8(""), alertPanel);
    m_alertLabel->setObjectName("alertLabel");
    m_alertLabel->setAlignment(Qt::AlignCenter);
    m_alertLabel->setFixedHeight(40);
    m_alertLabel->setVisible(false);
    alertInner->addWidget(m_alertLabel);

    m_clearAlertButton = new QPushButton(QString::fromUtf8("解除警报"), alertPanel);
    m_clearAlertButton->setObjectName("dangerButton");
    m_clearAlertButton->setEnabled(false);
    connect(m_clearAlertButton, &QPushButton::clicked, this, &Widget::sendClearAlert);
    alertInner->addWidget(m_clearAlertButton);

    m_watchdogStatus = new QLabel(QString::fromUtf8(""), alertPanel);
    m_watchdogStatus->setObjectName("watchdogStatus");
    m_watchdogStatus->setAlignment(Qt::AlignCenter);
    m_watchdogStatus->setVisible(false);
    alertInner->addWidget(m_watchdogStatus);

    leftLayout->addWidget(alertPanel);

    // --- Threshold config panel (Task-E) ---
    auto *thresholdPanel = createPanel(QString::fromUtf8("报警配置"));
    auto *thresholdGrid = new QGridLayout();
    thresholdGrid->setHorizontalSpacing(8);
    thresholdGrid->setVerticalSpacing(8);

    m_thresholdForceEdit = createNumberEdit("10.0");
    m_thresholdHoldoffEdit = createNumberEdit("3.0");

    thresholdGrid->addWidget(new QLabel(QString::fromUtf8("力值阈值 (N)")), 0, 0);
    thresholdGrid->addWidget(m_thresholdForceEdit, 0, 1);
    thresholdGrid->addWidget(new QLabel(QString::fromUtf8("持续时间 (s)")), 1, 0);
    thresholdGrid->addWidget(m_thresholdHoldoffEdit, 1, 1);

    m_sendThresholdButton = new QPushButton(QString::fromUtf8("下发至设备"), thresholdPanel);
    m_sendThresholdButton->setObjectName("primaryButton");
    m_sendThresholdButton->setEnabled(false);
    connect(m_sendThresholdButton, &QPushButton::clicked, this, &Widget::sendThresholdConfig);
    thresholdGrid->addWidget(m_sendThresholdButton, 2, 0, 1, 2);

    auto *thresholdInner = qobject_cast<QVBoxLayout *>(thresholdPanel->layout());
    thresholdInner->addLayout(thresholdGrid);

    auto *thresholdHint = new QLabel(QString::fromUtf8("修改后点击\"下发至设备\"，ESP32 将保存新阈值到 NVS。"), thresholdPanel);
    thresholdHint->setObjectName("hintLabel");
    thresholdHint->setWordWrap(true);
    thresholdInner->addWidget(thresholdHint);
    leftLayout->addWidget(thresholdPanel);

    auto *right = new QWidget(content);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    auto *forceChartPanel = createPanel(QString::fromUtf8("力波形"));
    auto *forceChartInner = qobject_cast<QVBoxLayout *>(forceChartPanel->layout());
    m_forceChart = new RealtimeChart(forceChartPanel);
    m_forceChart->addSeries(QString::fromUtf8("Force"), QColor("#22c55e"));
    m_forceChart->setUnit("N");
    forceChartInner->addWidget(m_forceChart, 1);
    rightLayout->addWidget(forceChartPanel, 3);

    auto *resChartPanel = createPanel(QString::fromUtf8("电阻与电压趋势"));
    auto *resChartInner = qobject_cast<QVBoxLayout *>(resChartPanel->layout());
    m_resistanceChart = new RealtimeChart(resChartPanel);
    m_resistanceChart->addSeries(QString::fromUtf8("Resistance MΩ"), QColor("#60a5fa"));
    m_resistanceChart->addSeries(QString::fromUtf8("Voltage V"), QColor("#f59e0b"));
    m_resistanceChart->setUnit(QString::fromUtf8("MΩ / V"));
    resChartInner->addWidget(m_resistanceChart, 1);
    rightLayout->addWidget(resChartPanel, 2);

    auto *leftScroll = new QScrollArea(content);
    leftScroll->setWidget(left);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setObjectName("leftScroll");

    contentLayout->addWidget(leftScroll, 36);
    contentLayout->addWidget(right, 64);
    static_cast<QVBoxLayout *>(this->layout())->addWidget(content, 1);
}

// =====================================================================
// 底部栏 — PC / 移动端
// =====================================================================

void Widget::setupBottomBar()
{
#ifdef Q_OS_ANDROID
    setupMobileBottomBar();
    return;
#endif

    auto *bottom = new QFrame(this);
    bottom->setObjectName("bottomBar");
    bottom->setFixedHeight(44);

    auto *layout = new QHBoxLayout(bottom);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(18);

    auto *ipLabel = new QLabel(QString::fromUtf8("本机地址: %1").arg(localIpHint()), bottom);
    ipLabel->setObjectName("bottomText");
    layout->addWidget(ipLabel);

    m_clientText = new QLabel(QString::fromUtf8("客户端: --"), bottom);
    m_clientText->setObjectName("bottomText");
    layout->addWidget(m_clientText);

    layout->addStretch(1);

    m_sampleCountText = new QLabel(QString::fromUtf8("样本: 0"), bottom);
    m_sampleCountText->setObjectName("bottomText");
    layout->addWidget(m_sampleCountText);

    m_rateText = new QLabel(QString::fromUtf8("速率: 0.0 Hz"), bottom);
    m_rateText->setObjectName("bottomText");
    layout->addWidget(m_rateText);

    static_cast<QVBoxLayout *>(this->layout())->addWidget(bottom);
}

// =====================================================================
// 移动端主内容 — 4 Tab
// =====================================================================

void Widget::setupMobileContent()
{
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("mobileTabs");
    tabs->setTabPosition(QTabWidget::South);
    tabs->setDocumentMode(true);
    tabs->tabBar()->setExpanding(true);
    tabs->tabBar()->setUsesScrollButtons(false);
    tabs->tabBar()->setElideMode(Qt::ElideNone);

    // ── Tab 1: 总览 ──
    auto *overviewPage = new QWidget(tabs);
    auto *overviewScroll = new QScrollArea(overviewPage);
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setFrameShape(QFrame::NoFrame);
    overviewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *overviewContent = new QWidget();
    auto *overviewLayout = new QVBoxLayout(overviewContent);
    overviewLayout->setContentsMargins(6, 6, 6, 6);
    overviewLayout->setSpacing(6);

    auto *metricsPanel = createPanel(QString::fromUtf8("实时数据"));
    auto *metricsGrid = new QGridLayout();
    metricsGrid->setHorizontalSpacing(6);
    metricsGrid->setVerticalSpacing(4);
    metricsGrid->setColumnStretch(0, 1);
    metricsGrid->setColumnStretch(1, 1);

    m_resistanceValue = createMetricValue();
    m_forceValue = createMetricValue();
    m_voltageValue = createMetricValue();
    m_adcValue = createMetricValue();
    m_timestampValue = createMetricValue();
    m_dataSourceValue = createMetricValue(QString::fromUtf8("等待数据"));

    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("电阻")), 0, 0);
    metricsGrid->addWidget(m_resistanceValue, 1, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("力")), 0, 1);
    metricsGrid->addWidget(m_forceValue, 1, 1);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("电压")), 2, 0);
    metricsGrid->addWidget(m_voltageValue, 3, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("ADC")), 2, 1);
    metricsGrid->addWidget(m_adcValue, 3, 1);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("时间")), 4, 0);
    metricsGrid->addWidget(m_timestampValue, 5, 0);
    metricsGrid->addWidget(createMetricCaption(QString::fromUtf8("来源")), 4, 1);
    metricsGrid->addWidget(m_dataSourceValue, 5, 1);

    qobject_cast<QVBoxLayout *>(metricsPanel->layout())->addLayout(metricsGrid);
    overviewLayout->addWidget(metricsPanel);

    auto *forcePanel = createPanel(QString::fromUtf8("力值状态"));
    auto *forceInner = qobject_cast<QVBoxLayout *>(forcePanel->layout());
    m_forceBar = new QProgressBar(forcePanel);
    m_forceBar->setObjectName("forceBar");
    m_forceBar->setRange(0, 1000);
    m_forceBar->setTextVisible(false);
    m_forceBar->setFixedHeight(16);
    forceInner->addWidget(m_forceBar);
    m_baselineStatus = new QLabel(QString::fromUtf8("零点: 等待连接..."), forcePanel);
    m_baselineStatus->setObjectName("hintLabel");
    forceInner->addWidget(m_baselineStatus);

    m_breathStatusLabel = new QLabel(QString::fromUtf8("呼吸状态: 等待数据..."), forcePanel);
    m_breathStatusLabel->setObjectName("breathStatus");
    forceInner->addWidget(m_breathStatusLabel);

    m_calibrateButton = new QPushButton(QString::fromUtf8("手动校准零点"), forcePanel);
    m_calibrateButton->setObjectName("secondaryButton");
    connect(m_calibrateButton, &QPushButton::clicked, this, &Widget::triggerCalibrate);
    forceInner->addWidget(m_calibrateButton);

    overviewLayout->addWidget(forcePanel);

    auto *healthReportPanel = createPanel(QString::fromUtf8("呼吸健康报告"));
    auto *healthReportInner = qobject_cast<QVBoxLayout *>(healthReportPanel->layout());

    m_healthReportSummary = new QLabel(QString::fromUtf8("暂无数据，请先采集呼吸数据"), healthReportPanel);
    m_healthReportSummary->setObjectName("hintLabel");
    m_healthReportSummary->setWordWrap(true);
    m_healthReportSummary->setStyleSheet("font-size:14px; color:#A0A0A0; padding:4px 0;");
    healthReportInner->addWidget(m_healthReportSummary);

    m_healthReportButton = new QPushButton(QString::fromUtf8("生成报告"), healthReportPanel);
    m_healthReportButton->setObjectName("primaryButton");
    connect(m_healthReportButton, &QPushButton::clicked, this, &Widget::openHealthReport);
    healthReportInner->addWidget(m_healthReportButton);

    overviewLayout->addWidget(healthReportPanel);
    overviewLayout->addStretch(1);

    auto *overviewScrollHint = new QLabel(QString::fromUtf8("↓ 下滑查看更多"), overviewContent);
    overviewScrollHint->setObjectName("scrollHint");
    overviewLayout->addWidget(overviewScrollHint);
    overviewLayout->addSpacing(48);

    overviewContent->setMinimumHeight(overviewLayout->minimumSize().height());
    overviewScroll->setWidget(overviewContent);
    QScroller::grabGesture(overviewScroll, QScroller::TouchGesture);
    QScroller *overviewScroller = QScroller::scroller(overviewScroll);
    QScrollerProperties op = overviewScroller->scrollerProperties();
    op.setScrollMetric(QScrollerProperties::DragStartDistance, 0.003);
    op.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.05);
    op.setScrollMetric(QScrollerProperties::MaximumVelocity, 2.5);
    op.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.08);
    op.setScrollMetric(QScrollerProperties::ScrollingCurve,
                       QEasingCurve(QEasingCurve::OutQuint));
    overviewScroller->setScrollerProperties(op);
    auto *overviewPageLayout = new QVBoxLayout(overviewPage);
    overviewPageLayout->setContentsMargins(0, 0, 0, 0);
    overviewPageLayout->addWidget(overviewScroll);

    tabs->addTab(overviewPage, QString::fromUtf8("总览"));

    // ── Tab 2: 波形 ──
    auto *chartPage = new QWidget(tabs);
    auto *chartLayout = new QVBoxLayout(chartPage);
    chartLayout->setContentsMargins(6, 6, 6, 6);
    chartLayout->setSpacing(6);

    auto *forceChartPanel = createPanel(QString::fromUtf8("力波形"));
    auto *forceChartInner = qobject_cast<QVBoxLayout *>(forceChartPanel->layout());
    m_forceChart = new RealtimeChart(forceChartPanel);
    m_forceChart->addSeries(QString::fromUtf8("Force"), QColor("#22c55e"));
    m_forceChart->setUnit("N");
    forceChartInner->addWidget(m_forceChart, 1);
    chartLayout->addWidget(forceChartPanel, 1);

    auto *resChartPanel = createPanel(QString::fromUtf8("电阻与电压趋势"));
    auto *resChartInner = qobject_cast<QVBoxLayout *>(resChartPanel->layout());
    m_resistanceChart = new RealtimeChart(resChartPanel);
    m_resistanceChart->addSeries(QString::fromUtf8("Resistance MΩ"), QColor("#60a5fa"));
    m_resistanceChart->addSeries(QString::fromUtf8("Voltage V"), QColor("#f59e0b"));
    m_resistanceChart->setUnit(QString::fromUtf8("MΩ / V"));
    resChartInner->addWidget(m_resistanceChart, 1);
    chartLayout->addWidget(resChartPanel, 1);
    tabs->addTab(chartPage, QString::fromUtf8("波形"));

    // ── Tab 3: 参数 ──
    auto *paramsPage = new QWidget(tabs);
    auto *paramsScroll = new QScrollArea(paramsPage);
    paramsScroll->setWidgetResizable(true);
    paramsScroll->setFrameShape(QFrame::NoFrame);
    paramsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *paramsContent = new QWidget();
    auto *paramsLayout = new QVBoxLayout(paramsContent);
    paramsLayout->setContentsMargins(6, 6, 6, 6);
    paramsLayout->setSpacing(12);

    auto *calPanel = createPanel(QString::fromUtf8("换算参数"));
    auto *calGrid = new QGridLayout();
    calGrid->setHorizontalSpacing(10);
    calGrid->setVerticalSpacing(10);
    calGrid->setColumnStretch(0, 0);
    calGrid->setColumnStretch(1, 1);

    m_refEdit = createNumberEdit("10.0");
    m_refEdit->setMinimumWidth(80);
    m_vccEdit = createNumberEdit("3300");
    m_vccEdit->setMinimumWidth(80);
    m_zeroEdit = createNumberEdit("--");
    m_zeroEdit->setReadOnly(true);
    m_zeroEdit->setMinimumWidth(80);
    m_zeroEdit->setToolTip(QString::fromUtf8("自动标定：连接后 10 秒采集的电阻平均值"));
    m_scaleEdit = createNumberEdit("1.0");
    m_scaleEdit->setMinimumWidth(80);

    auto *lblRef = new QLabel(QString::fromUtf8("参考电阻"), calPanel);
    lblRef->setObjectName("inlineLabel");
    lblRef->setMinimumWidth(70);
    auto *lblVcc = new QLabel(QString::fromUtf8("供电电压"), calPanel);
    lblVcc->setObjectName("inlineLabel");
    lblVcc->setMinimumWidth(70);
    auto *lblZero = new QLabel(QString::fromUtf8("零点电阻"), calPanel);
    lblZero->setObjectName("inlineLabel");
    lblZero->setMinimumWidth(70);
    auto *lblScale = new QLabel(QString::fromUtf8("力系数"), calPanel);
    lblScale->setObjectName("inlineLabel");
    lblScale->setMinimumWidth(70);

    calGrid->addWidget(lblRef,   0, 0, Qt::AlignVCenter);
    calGrid->addWidget(m_refEdit,  0, 1);
    calGrid->addWidget(lblVcc,   1, 0, Qt::AlignVCenter);
    calGrid->addWidget(m_vccEdit,  1, 1);
    calGrid->addWidget(lblZero,  2, 0, Qt::AlignVCenter);
    calGrid->addWidget(m_zeroEdit, 2, 1);
    calGrid->addWidget(lblScale, 3, 0, Qt::AlignVCenter);
    calGrid->addWidget(m_scaleEdit,3, 1);
    qobject_cast<QVBoxLayout *>(calPanel->layout())->addLayout(calGrid);

    auto *calHint = new QLabel(
        QString::fromUtf8("参考电阻(MΩ) | 供电电压(mV) | 零点自动标定 | 力系数(N/MΩ)"),
        calPanel);
    calHint->setObjectName("hintLabel");
    calHint->setWordWrap(true);
    qobject_cast<QVBoxLayout *>(calPanel->layout())->addWidget(calHint);
    paramsLayout->addWidget(calPanel);

    auto *buttonPanel = createPanel(QString::fromUtf8("数据操作"));
    auto *buttonInner = qobject_cast<QVBoxLayout *>(buttonPanel->layout());
    buttonInner->setContentsMargins(8, 4, 8, 6);
    buttonInner->setSpacing(6);
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);

    m_clearButton = new QPushButton(QString::fromUtf8("清空数据"), buttonPanel);
    m_clearButton->setObjectName("secondaryButton");
    m_clearButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_clearButton, &QPushButton::clicked, this, &Widget::clearData);
    buttonLayout->addWidget(m_clearButton);

    m_simButton = new QPushButton(QString::fromUtf8("模拟数据"), buttonPanel);
    m_simButton->setObjectName("secondaryButton");
    m_simButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_simButton, &QPushButton::clicked, this, [this]() {
        if (m_simTimer->isActive()) {
            m_simTimer->stop();
            m_simButton->setText(QString::fromUtf8("模拟数据"));
        } else {
            m_simTimer->start(simulationIntervalMs());
            m_simButton->setText(QString::fromUtf8("停止模拟"));
        }
    });
    buttonLayout->addWidget(m_simButton);
    buttonInner->addLayout(buttonLayout);

    auto *speedLayout = new QHBoxLayout();
    speedLayout->setContentsMargins(0, 0, 0, 0);
    auto *speedLabel = new QLabel(QString::fromUtf8("模拟速度"), buttonPanel);
    speedLabel->setObjectName("inlineLabel");
    speedLayout->addWidget(speedLabel);
    m_simSpeedSlider = new QSlider(Qt::Horizontal, buttonPanel);
    m_simSpeedSlider->setObjectName("speedSlider");
    m_simSpeedSlider->setRange(1, 10);
    speedLayout->addWidget(m_simSpeedSlider, 1);
    m_simSpeedText = new QLabel(QString::fromUtf8("4x"), buttonPanel);
    m_simSpeedText->setObjectName("speedText");
    m_simSpeedText->setMinimumWidth(34);
    m_simSpeedText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_simSpeedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_simSpeedText->setText(QString::fromUtf8("%1x").arg(value));
        m_settings->setValue("simulation/speed", value);
        if (m_simTimer->isActive()) {
            m_simTimer->start(simulationIntervalMs());
        }
    });
    speedLayout->addWidget(m_simSpeedText);
    buttonInner->addLayout(speedLayout);
    paramsLayout->addWidget(buttonPanel);

    auto *collectPanel = createPanel(QString::fromUtf8("数据采集"));
    auto *collectInner = qobject_cast<QVBoxLayout *>(collectPanel->layout());
    collectInner->setContentsMargins(8, 4, 8, 6);
    collectInner->setSpacing(6);

    auto *forceRow = new QHBoxLayout();
    forceRow->setContentsMargins(0, 0, 0, 0);
    forceRow->setSpacing(8);
    auto *forceLabel = new QLabel(QString::fromUtf8("施加力 (N)"), collectPanel);
    forceLabel->setObjectName("inlineLabel");
    forceRow->addWidget(forceLabel);
    m_forceInput = new QLineEdit("0.0", collectPanel);
    m_forceInput->setObjectName("numberEdit");
    m_forceInput->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_forceInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    forceRow->addWidget(m_forceInput, 1);
    collectInner->addLayout(forceRow);

    auto *collectBtnRow = new QHBoxLayout();
    collectBtnRow->setContentsMargins(0, 0, 0, 0);
    collectBtnRow->setSpacing(8);
    m_collectButton = new QPushButton(QString::fromUtf8("采集"), collectPanel);
    m_collectButton->setObjectName("secondaryButton");
    m_collectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_collectButton, &QPushButton::clicked, this, &Widget::toggleCollect);
    collectBtnRow->addWidget(m_collectButton);
    m_saveButton = new QPushButton(QString::fromUtf8("保存"), collectPanel);
    m_saveButton->setObjectName("secondaryButton");
    m_saveButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_saveButton, &QPushButton::clicked, this, &Widget::saveCollectedData);
    collectBtnRow->addWidget(m_saveButton);
    collectInner->addLayout(collectBtnRow);

    m_collectStatus = new QLabel(QString::fromUtf8("状态: 已采集 0 条"), collectPanel);
    m_collectStatus->setObjectName("hintLabel");
    collectInner->addWidget(m_collectStatus);

    m_breathModeCheck = new QCheckBox(QString::fromUtf8("呼吸模式（自动记录力值）"), collectPanel);
    m_breathModeCheck->setObjectName("hintLabel");
    collectInner->addWidget(m_breathModeCheck);
    paramsLayout->addWidget(collectPanel);

    auto *alertPanel = createPanel(QString::fromUtf8("报警状态"));
    auto *alertInner = qobject_cast<QVBoxLayout *>(alertPanel->layout());

    m_alertLabel = new QLabel(QString::fromUtf8(""), alertPanel);
    m_alertLabel->setObjectName("alertLabel");
    m_alertLabel->setAlignment(Qt::AlignCenter);
    m_alertLabel->setFixedHeight(40);
    m_alertLabel->setVisible(false);
    alertInner->addWidget(m_alertLabel);

    m_clearAlertButton = new QPushButton(QString::fromUtf8("解除警报"), alertPanel);
    m_clearAlertButton->setObjectName("dangerButton");
    m_clearAlertButton->setEnabled(false);
    connect(m_clearAlertButton, &QPushButton::clicked, this, &Widget::sendClearAlert);
    alertInner->addWidget(m_clearAlertButton);

    m_watchdogStatus = new QLabel(QString::fromUtf8(""), alertPanel);
    m_watchdogStatus->setObjectName("watchdogStatus");
    m_watchdogStatus->setAlignment(Qt::AlignCenter);
    m_watchdogStatus->setVisible(false);
    alertInner->addWidget(m_watchdogStatus);
    paramsLayout->addWidget(alertPanel);

    auto *thresholdPanel = createPanel(QString::fromUtf8("报警配置"));
    auto *thresholdGrid = new QGridLayout();
    thresholdGrid->setHorizontalSpacing(10);
    thresholdGrid->setVerticalSpacing(10);
    thresholdGrid->setColumnStretch(0, 0);
    thresholdGrid->setColumnStretch(1, 1);

    m_thresholdForceEdit = createNumberEdit("10.0");
    m_thresholdForceEdit->setMinimumWidth(80);
    m_thresholdHoldoffEdit = createNumberEdit("3.0");
    m_thresholdHoldoffEdit->setMinimumWidth(80);

    auto *lblForce = new QLabel(QString::fromUtf8("力值阈值"), thresholdPanel);
    lblForce->setObjectName("inlineLabel");
    lblForce->setMinimumWidth(70);
    auto *lblHoldoff = new QLabel(QString::fromUtf8("持续时间"), thresholdPanel);
    lblHoldoff->setObjectName("inlineLabel");
    lblHoldoff->setMinimumWidth(70);

    thresholdGrid->addWidget(lblForce,   0, 0, Qt::AlignVCenter);
    thresholdGrid->addWidget(m_thresholdForceEdit, 0, 1);
    thresholdGrid->addWidget(lblHoldoff, 1, 0, Qt::AlignVCenter);
    thresholdGrid->addWidget(m_thresholdHoldoffEdit, 1, 1);

    m_sendThresholdButton = new QPushButton(QString::fromUtf8("下发至设备"), thresholdPanel);
    m_sendThresholdButton->setObjectName("primaryButton");
    m_sendThresholdButton->setEnabled(false);
    connect(m_sendThresholdButton, &QPushButton::clicked, this, &Widget::sendThresholdConfig);
    thresholdGrid->addWidget(m_sendThresholdButton, 2, 0, 1, 2);

    auto *thresholdInner = qobject_cast<QVBoxLayout *>(thresholdPanel->layout());
    thresholdInner->addLayout(thresholdGrid);

    auto *thresholdHint = new QLabel(
        QString::fromUtf8("力值阈值单位：牛顿(N) | 持续时间单位：秒(s) | 下发后 ESP32 自动保存"),
        thresholdPanel);
    thresholdHint->setObjectName("hintLabel");
    thresholdHint->setWordWrap(true);
    thresholdInner->addWidget(thresholdHint);
    paramsLayout->addWidget(thresholdPanel);

    auto *paramsScrollHint = new QLabel(QString::fromUtf8("↓ 下滑查看更多"), paramsContent);
    paramsScrollHint->setObjectName("scrollHint");
    paramsLayout->addWidget(paramsScrollHint);
    paramsLayout->addSpacing(60);

    paramsContent->setMinimumHeight(paramsLayout->minimumSize().height());
    paramsScroll->setWidget(paramsContent);
    QScroller::grabGesture(paramsScroll, QScroller::TouchGesture);
    QScroller *paramsScroller = QScroller::scroller(paramsScroll);
    QScrollerProperties sp = paramsScroller->scrollerProperties();
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.003);
    sp.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.05);
    sp.setScrollMetric(QScrollerProperties::MaximumVelocity, 2.5);
    sp.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.08);
    sp.setScrollMetric(QScrollerProperties::ScrollingCurve,
                       QEasingCurve(QEasingCurve::OutQuint));
    paramsScroller->setScrollerProperties(sp);
    auto *paramsPageLayout = new QVBoxLayout(paramsPage);
    paramsPageLayout->setContentsMargins(0, 0, 0, 0);
    paramsPageLayout->addWidget(paramsScroll);
    tabs->addTab(paramsPage, QString::fromUtf8("参数"));

    // ── Tab 4: 连接 ──
    auto *connectionPage = new QWidget(tabs);
    auto *connectionLayout = new QVBoxLayout(connectionPage);
    connectionLayout->setContentsMargins(6, 6, 6, 6);
    connectionLayout->setSpacing(6);

    auto *serverPanel = createPanel(QString::fromUtf8("连接设置"));
    auto *serverInner = qobject_cast<QVBoxLayout *>(serverPanel->layout());
    auto *portLayout = new QHBoxLayout();
    portLayout->setContentsMargins(0, 0, 0, 0);
    auto *portLabel = new QLabel(QString::fromUtf8("监听端口"), serverPanel);
    portLabel->setObjectName("inlineLabel");
    portLayout->addWidget(portLabel);
    m_portEdit = new QLineEdit("8080", serverPanel);
    m_portEdit->setObjectName("numberEdit");
    m_portEdit->setAlignment(Qt::AlignCenter);
    portLayout->addWidget(m_portEdit, 1);
    serverInner->addLayout(portLayout);

    auto *serverButtons = new QHBoxLayout();
    serverButtons->setContentsMargins(0, 0, 0, 0);
    m_startButton = new QPushButton(QString::fromUtf8("开始监听"), serverPanel);
    m_startButton->setObjectName("primaryButton");
    connect(m_startButton, &QPushButton::clicked, this, &Widget::startServer);
    serverButtons->addWidget(m_startButton);
    m_stopButton = new QPushButton(QString::fromUtf8("停止"), serverPanel);
    m_stopButton->setObjectName("secondaryButton");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &Widget::stopServer);
    serverButtons->addWidget(m_stopButton);
    serverInner->addLayout(serverButtons);
    connectionLayout->addWidget(serverPanel);

    auto *statusPanel = createPanel(QString::fromUtf8("网络状态"));
    auto *statusInner = qobject_cast<QVBoxLayout *>(statusPanel->layout());
    auto *ipText = new QLabel(QString::fromUtf8("本机地址: %1").arg(localIpHint()), statusPanel);
    ipText->setObjectName("bottomText");
    ipText->setWordWrap(true);
    statusInner->addWidget(ipText);
    m_clientText = new QLabel(QString::fromUtf8("客户端: --"), statusPanel);
    m_clientText->setObjectName("bottomText");
    m_clientText->setWordWrap(true);
    statusInner->addWidget(m_clientText);
    m_sampleCountText = new QLabel(QString::fromUtf8("样本: 0"), statusPanel);
    m_sampleCountText->setObjectName("bottomText");
    statusInner->addWidget(m_sampleCountText);
    m_rateText = new QLabel(QString::fromUtf8("速率: 0.0 Hz"), statusPanel);
    m_rateText->setObjectName("bottomText");
    statusInner->addWidget(m_rateText);
    connectionLayout->addWidget(statusPanel);
    connectionLayout->addStretch(1);
    tabs->addTab(connectionPage, QString::fromUtf8("连接"));

    static_cast<QVBoxLayout *>(this->layout())->addWidget(tabs, 1);
}

void Widget::setupMobileBottomBar()
{
}

// =====================================================================
// UI 工厂函数
// =====================================================================

QFrame *Widget::createPanel(const QString &title)
{
    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    panel->setFrameShape(QFrame::NoFrame);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(panel);
#ifdef Q_OS_ANDROID
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
#else
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);
#endif

    auto *titleLabel = new QLabel(title, panel);
    titleLabel->setObjectName("panelTitle");
    layout->addWidget(titleLabel);
    return panel;
}

QLabel *Widget::createMetricValue(const QString &text)
{
    auto *label = new QLabel(text, this);
    label->setObjectName("metricValue");
    label->setMinimumHeight(32);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

QLabel *Widget::createMetricCaption(const QString &text)
{
    auto *label = new QLabel(text, this);
    label->setObjectName("metricCaption");
    return label;
}

QLineEdit *Widget::createNumberEdit(const QString &text, int width)
{
    auto *edit = new QLineEdit(text, this);
    edit->setObjectName("numberEdit");
    edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
#ifdef Q_OS_ANDROID
    Q_UNUSED(width);
#else
    edit->setFixedWidth(width);
#endif
    connect(edit, &QLineEdit::editingFinished, this, &Widget::updateCalibrationFromUi);
    return edit;
}

// =====================================================================
// 全局 QSS 样式表
// =====================================================================

void Widget::applyStyles()
{
    // ── 色板：融合 Serial Studio fluent-dark + FluentUI ──
    // 主背景   #10131a    卡片背景  #171c26
    // 卡片边框 #3D3D3D    主按钮    #0078D4
    // 次按钮   #2D2D2D    次按钮边框 #505050
    // 主文字   #F0F0F0    次文字    #A0A0A0
    // 强调蓝   #0078D4    安全绿    #4CAF50
    // 告警黄   #FFB74D    告警红    #FF5252

    setStyleSheet(R"(
        /* ── 全局 ── */
        QWidget {
            background: #10131a;
            color: #F0F0F0;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
        }

        /* ── 顶部栏 ── */
        QFrame#topBar {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #121827, stop:1 #182235);
            border-bottom: 1px solid #3D3D3D;
        }
        QLabel#titleLabel {
            font-size: 20px;
            font-weight: 700;
            color: #F0F0F0;
        }
        QLabel#subtitleLabel, QLabel#statusText, QLabel#inlineLabel {
            color: #A0A0A0;
            font-size: 12px;
        }
        QLabel#speedText {
            color: #E0E0E0;
            font-family: "Consolas", "Cascadia Code", monospace;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#statusDot {
            border-radius: 6px;
            background: #FF5252;
        }

        /* ── 卡片面板（Serial Studio Pane 风格） ── */
        QFrame#panel {
            background: #171c26;
            border: 1px solid #3D3D3D;
            border-radius: 4px;
        }
        QLabel#panelTitle {
            color: #A0A0A0;
            font-size: 12px;
            font-weight: 700;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #1a2233, stop:1 #222e42);
            padding: 4px 8px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }

        /* ── 指标数据 ── */
        QLabel#metricCaption {
            color: #808080;
            font-size: 12px;
        }
        QLabel#metricValue {
            color: #F0F0F0;
            font-family: "Consolas", "Cascadia Code", monospace;
            font-size: 25px;
            font-weight: 700;
        }
        QTabWidget#mobileTabs QLabel#metricValue {
            font-size: 20px;
        }

        /* ── 提示文字 / 呼吸状态 ── */
        QLabel#hintLabel {
            color: #808080;
            font-size: 12px;
            line-height: 1.35;
        }
        QLabel#breathStatus {
            color: #E0E0E0;
            font-size: 14px;
            font-weight: 700;
            padding: 4px 0;
        }
        QLabel#bottomText {
            color: #A0A0A0;
            font-size: 12px;
        }

        /* ── 底部栏 ── */
        QFrame#bottomBar {
            background: #0f141d;
            border-top: 1px solid #3D3D3D;
        }

        /* ── 移动端 Tab 导航（底部指示条风格） ── */
        QTabWidget#mobileTabs::pane {
            border: none;
            background: #10131a;
        }
        QTabWidget#mobileTabs QTabBar::tab {
            background: #121827;
            color: #A0A0A0;
            border: none;
            border-top: 2px solid transparent;
            min-width: 72px;
            min-height: 42px;
            padding: 8px 0;
            font-size: 13px;
            font-weight: 700;
        }
        QTabWidget#mobileTabs QTabBar::tab:selected {
            background: #171c26;
            color: #F0F0F0;
            border-top: 2px solid #0078D4;
        }
        QTabWidget#mobileTabs QTabBar::tab:!selected {
            margin-top: 0px;
        }
        QTabWidget#mobileTabs QTabBar::tab:hover:!selected {
            background: #252525;
            color: #D0D0D0;
        }

        /* ── 移动端控件尺寸 ── */
        QTabWidget#mobileTabs QLineEdit#numberEdit {
            padding: 6px 8px;
            font-size: 15px;
            min-height: 24px;
        }
        QTabWidget#mobileTabs QPushButton#primaryButton {
            padding: 6px 12px;
            font-size: 13px;
            min-height: 18px;
        }
        QTabWidget#mobileTabs QPushButton#secondaryButton {
            padding: 5px 10px;
            font-size: 13px;
            min-height: 18px;
        }
        QTabWidget#mobileTabs QPushButton#dangerButton {
            padding: 6px 12px;
            font-size: 13px;
        }
        QTabWidget#mobileTabs QCheckBox {
            font-size: 14px;
            spacing: 8px;
            min-height: 24px;
        }

        /* ── 输入框（FluentUI 底部指示条风格） ── */
        QLineEdit#compactEdit, QLineEdit#numberEdit {
            background: #2D2D2D;
            border: 1px solid #505050;
            border-bottom: 2px solid #505050;
            border-radius: 4px;
            color: #F0F0F0;
            padding: 5px 7px;
            selection-background-color: #0078D4;
        }
        QLineEdit#compactEdit:focus, QLineEdit#numberEdit:focus {
            border: 1px solid #0078D4;
            border-bottom: 2px solid #0078D4;
        }
        QLineEdit#numberEdit:read-only {
            background: #252525;
            color: #808080;
        }

        /* ── 主按钮（Fluent FilledButton 风格） ── */
        QPushButton#primaryButton {
            background: #0078D4;
            color: #ffffff;
            border: 1px solid #005A9E;
            border-radius: 4px;
            padding: 7px 16px;
            font-weight: 700;
        }
        QPushButton#primaryButton:hover {
            background: #1A86D8;
        }
        QPushButton#primaryButton:pressed {
            background: #005A9E;
        }
        QPushButton#primaryButton:disabled {
            background: #3D3D3D;
            color: #6D6D6D;
            border-color: #3D3D3D;
        }

        /* ── 次按钮（Fluent Button 风格） ── */
        QPushButton#secondaryButton {
            background: #2D2D2D;
            color: #E0E0E0;
            border: 1px solid #505050;
            border-radius: 4px;
            padding: 7px 14px;
            font-weight: 600;
        }
        QPushButton#secondaryButton:hover {
            background: #383838;
        }
        QPushButton#secondaryButton:pressed {
            background: #252525;
        }
        QPushButton#secondaryButton:disabled {
            color: #6D6D6D;
            background: #252525;
            border-color: #3D3D3D;
        }

        /* ── 进度条（Serial Studio Bar 渐变风格） ── */
        QProgressBar#forceBar {
            background: #252525;
            border: 1px solid #3D3D3D;
            border-radius: 4px;
        }
        QProgressBar#forceBar::chunk {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                stop:0 #66BB6A, stop:0.5 #4CAF50, stop:1 #388E3C);
            border-radius: 3px;
        }

        /* ── 滑块 ── */
        QSlider#speedSlider::groove:horizontal {
            height: 4px;
            background: #2D2D2D;
            border: 1px solid #3D3D3D;
            border-radius: 2px;
        }
        QSlider#speedSlider::sub-page:horizontal {
            background: #4CAF50;
            border-radius: 2px;
        }
        QSlider#speedSlider::handle:horizontal {
            width: 14px;
            height: 14px;
            margin: -6px 0;
            border-radius: 7px;
            background: #E0E0E0;
            border: 1px solid #808080;
        }

        /* ── 报警（Serial Studio 三色体系） ── */
        QLabel#alertLabel {
            color: #ffffff;
            font-size: 18px;
            font-weight: 800;
            background: #FF5252;
            border-radius: 4px;
            padding: 6px;
        }
        QPushButton#dangerButton {
            background: #D32F2F;
            color: #ffffff;
            border: 1px solid #FF5252;
            border-radius: 4px;
            padding: 7px 14px;
            font-weight: 700;
            font-size: 14px;
        }
        QPushButton#dangerButton:hover {
            background: #F44336;
        }
        QPushButton#dangerButton:pressed {
            background: #B71C1C;
        }
        QPushButton#dangerButton:disabled {
            background: #3D2020;
            color: #6D4444;
            border-color: #5C2020;
        }
        QLabel#watchdogStatus {
            color: #FFB74D;
            font-size: 13px;
            font-weight: 700;
            padding: 4px;
        }

        /* ── 滚动区域 ── */
        QScrollArea#leftScroll {
            background: transparent;
            border: none;
        }
        QScrollArea#leftScroll > QWidget > QWidget {
            background: transparent;
        }
        QTabWidget#mobileTabs QScrollArea {
            background: #10131a;
            border: none;
        }
        QTabWidget#mobileTabs QScrollArea > QWidget > QWidget {
            background: #10131a;
        }

        /* ── 滚动条（3px 极简风格） ── */
        QScrollBar:vertical {
            background: transparent;
            width: 3px;
            margin: 0;
            border-radius: 1px;
        }
        QScrollBar::handle:vertical {
            background: #505050;
            min-height: 32px;
            border-radius: 1px;
        }
        QScrollBar::handle:vertical:hover {
            background: #808080;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }

        /* ── 滚动提示 ── */
        QLabel#scrollHint {
            color: #505050;
            font-size: 11px;
            padding: 8px 0 2px 0;
            qproperty-alignment: 'AlignHCenter';
        }
    )");
}
