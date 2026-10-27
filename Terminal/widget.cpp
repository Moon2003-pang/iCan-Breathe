// widget.cpp — 构造/析构、UI 刷新循环、工具函数
// UI 布局 → widget_ui.cpp | 通信 → widget_comm.cpp | 报警 → widget_alert.cpp | 数据 → widget_data.cpp

#include "widget.h"
#include "qslider.h"
#include "realtimechart.h"
#include "healthreportdialog.h"

#include <QApplication>
#include <QDateTime>
#include <QNetworkInterface>
#include <QProgressBar>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>

#include <cmath>

// =====================================================================
// 构造 / 析构
// =====================================================================

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , m_settings(new QSettings("Zhongyan", "ResistanceForceMonitor", this))
{
    setWindowTitle(QString::fromUtf8("电阻-力监测上位机"));
#ifdef Q_OS_ANDROID
    setMinimumSize(0, 0);
#else
    setMinimumSize(1120, 720);
    resize(1280, 820);

    if (auto *screen = QApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.center() - rect().center());
    }
#endif

    setupUi();
    applyStyles();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &Widget::acceptClient);

    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, &Widget::refreshUi);
    m_uiTimer->start(250);

    m_simTimer = new QTimer(this);
    connect(m_simTimer, &QTimer::timeout, this, &Widget::appendSimulatedSample);

    m_portEdit->setText(m_settings->value("server/port", 8080).toString());
    m_refEdit->setText(m_settings->value("calibration/refMohm", 10.0).toString());
    m_vccEdit->setText(m_settings->value("calibration/vccMv", 3300.0).toString());
    m_scaleEdit->setText(m_settings->value("calibration/scale", 1.0).toString());
    m_simSpeedSlider->setValue(m_settings->value("simulation/speed", 4).toInt());
    m_simSpeedText->setText(QString::fromUtf8("%1x").arg(m_simSpeedSlider->value()));

    m_alertBlinkTimer = new QTimer(this);
    m_alertBlinkTimer->setInterval(500);
    connect(m_alertBlinkTimer, &QTimer::timeout, this, &Widget::toggleAlertBlink);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setSingleShot(true);
    m_watchdogTimer->setInterval(5000);
    connect(m_watchdogTimer, &QTimer::timeout, this, &Widget::onWatchdogTimeout);

    m_reminderTimer = new QTimer(this);
    m_reminderTimer->setSingleShot(true);
    m_reminderTimer->setInterval(30000);
    connect(m_reminderTimer, &QTimer::timeout, this, &Widget::onReminderTimeout);

    loadEmergencyContacts();

    if (m_thresholdForceEdit) {
        m_thresholdForceEdit->setText(
            m_settings->value("alert/forceThresholdN", 10.0).toString());
    }
    if (m_thresholdHoldoffEdit) {
        m_thresholdHoldoffEdit->setText(
            m_settings->value("alert/holdoffSec", 3.0).toString());
    }

    initBreathHistory();

    updateCalibrationFromUi();
    updateConnectionState();
    updateMetrics();
}

Widget::~Widget()
{
    flushCsvBuffer();
    if (m_breathHistoryStream) {
        m_breathHistoryStream->flush();
        delete m_breathHistoryStream;
        m_breathHistoryStream = nullptr;
    }
    if (m_breathHistoryFile) {
        m_breathHistoryFile->close();
        delete m_breathHistoryFile;
        m_breathHistoryFile = nullptr;
    }

    stopServer();
    m_settings->sync();
}

// =====================================================================
// UI 刷新循环（250ms 定时器）
// =====================================================================

void Widget::refreshUi()
{
    updateMetrics();
    updateConnectionState();

    if (m_healthReportSummary && ++m_healthReportRefreshCounter >= 40) {
        m_healthReportRefreshCounter = 0;
        updateHealthReportSummary();
    }
}

void Widget::clearData()
{
    m_current = SensorSample{};
    m_sampleCount = 0;
    m_lastRateHz = 0.0;
    m_lastSampleWallMs = 0;
    m_forceChart->clear();
    m_resistanceChart->clear();

    m_baselineReady = false;
    m_baselineStartMs = 0;
    m_baselineR0 = 0.0;
    m_baselineBuffer.clear();
    m_adaptiveR0 = 0.0;
    m_adaptiveBaselineInit = false;

    m_breathBuffer.clear();
    if (m_breathStatusLabel) {
        m_breathStatusLabel->setText(QString::fromUtf8("呼吸状态: 等待数据..."));
    }

    if (m_alertActive) {
        m_alertActive = false;
        m_emergencySent = false;
        hideAlarm();
        m_reminderTimer->stop();
        if (m_reminderActive) {
            m_reminderActive = false;
            m_alertBlinkTimer->setInterval(500);
        }
    }

    updateMetrics();
    updateHealthReportSummary();
}

// =====================================================================
// 模拟数据生成
// =====================================================================

void Widget::appendSimulatedSample()
{
    static double t = 0.0;
    updateCalibrationFromUi();
    t += 0.08 * m_simSpeedSlider->value();

    const double noise = (QRandomGenerator::global()->bounded(1000) - 500) / 1000000.0;
    const double resistanceMohm = 10.0 + std::sin(t) * 0.8 + std::sin(t * 0.27) * 0.25 + noise;

    SensorSample sample;
    sample.timestampMs = QDateTime::currentMSecsSinceEpoch();
    sample.resistanceOhm = resistanceMohm * 1000000.0;
    sample.hasResistance = true;
    sample.forceN = forceFromResistance(sample.resistanceOhm);
    sample.hasForce = true;
    const double voltageMv = m_vccMv * sample.resistanceOhm / (m_refOhm + sample.resistanceOhm);
    sample.voltageMv = voltageMv;
    sample.hasVoltage = true;
    sample.adc = voltageMv / m_vccMv * 4095.0;
    sample.hasAdc = true;
    appendSample(sample);
}

// =====================================================================
// 连接状态 / 指标 / 波形更新
// =====================================================================

void Widget::updateConnectionState()
{
    if (m_client) {
        m_statusText->setText(QString::fromUtf8("已连接"));
        m_statusDot->setStyleSheet("background:#4CAF50;border-radius:6px;");
        m_clientText->setText(QString::fromUtf8("客户端: %1:%2")
                                  .arg(m_client->peerAddress().toString())
                                  .arg(m_client->peerPort()));
    } else if (m_server && m_server->isListening()) {
#ifdef Q_OS_ANDROID
        m_statusText->setText(QString::fromUtf8("监听中"));
#else
        m_statusText->setText(QString::fromUtf8("监听中:%1").arg(m_server->serverPort()));
#endif
        m_statusDot->setStyleSheet("background:#FFB74D;border-radius:6px;");
        m_clientText->setText(QString::fromUtf8("客户端: 等待 ESP32-S3"));
    } else {
        m_statusText->setText(QString::fromUtf8("未监听"));
        m_statusDot->setStyleSheet("background:#FF5252;border-radius:6px;");
        m_clientText->setText(QString::fromUtf8("客户端: --"));
    }

    updateThresholdButtonState();
}

void Widget::updateMetrics()
{
    m_resistanceValue->setText(m_current.hasResistance ? formatResistance(m_current.resistanceOhm) : "--");
    m_forceValue->setText(m_current.hasForce ? formatForce(m_current.forceN) : "--");
    m_voltageValue->setText(m_current.hasVoltage ? QString("%1 mV").arg(m_current.voltageMv, 0, 'f', 1) : "--");
    m_adcValue->setText(m_current.hasAdc ? QString::number(m_current.adc, 'f', 0) : "--");
#ifdef Q_OS_ANDROID
    m_timestampValue->setText(m_current.timestampMs > 0
                                  ? QDateTime::fromMSecsSinceEpoch(m_current.timestampMs).toString("HH:mm:ss")
                                  : "--");
#else
    m_timestampValue->setText(m_current.timestampMs > 0
                                  ? QDateTime::fromMSecsSinceEpoch(m_current.timestampMs).toString("HH:mm:ss.zzz")
                                  : "--");
#endif

    QString source = "--";
    if (m_current.hasResistance) {
#ifdef Q_OS_ANDROID
        source = QString::fromUtf8("电阻");
#else
        source = QString::fromUtf8("电阻字段/电压换算");
#endif
    } else if (m_current.hasVoltage) {
#ifdef Q_OS_ANDROID
        source = QString::fromUtf8("电压");
#else
        source = QString::fromUtf8("电压字段");
#endif
    } else if (m_current.hasAdc) {
#ifdef Q_OS_ANDROID
        source = QString::fromUtf8("ADC");
#else
        source = QString::fromUtf8("ADC 字段");
#endif
    }
    m_dataSourceValue->setText(source);

    const int forceProgress = m_current.hasForce
                                  ? qBound(0, static_cast<int>(qAbs(m_current.forceN) * 100.0), 1000)
                                  : 0;
    m_forceBar->setValue(forceProgress);
    m_sampleCountText->setText(QString::fromUtf8("样本: %1").arg(m_sampleCount));
    m_rateText->setText(QString::fromUtf8("速率: %1 Hz").arg(m_lastRateHz, 0, 'f', 1));

    if (m_baselineStatus) {
        if (m_baselineReady && m_adaptiveBaselineInit) {
            m_baselineStatus->setText(QString::fromUtf8("零点: 自适应 (R0=%1 Ω)")
                .arg(m_adaptiveR0, 0, 'f', 1));
        } else if (m_baselineReady) {
            m_baselineStatus->setText(QString::fromUtf8("零点: 就绪 (R0=%1 Ω)")
                .arg(m_baselineR0, 0, 'f', 1));
        } else if (m_baselineStartMs > 0) {
            qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_baselineStartMs;
            int secLeft = qMax(0, (kBaselineDurationMs - static_cast<int>(elapsed)) / 1000);
            m_baselineStatus->setText(QString::fromUtf8("零点: 校准中... (%1s)")
                .arg(10 - secLeft));
        } else {
            m_baselineStatus->setText(QString::fromUtf8("零点: 等待连接..."));
        }
    }
}

void Widget::updateCharts(const SensorSample &sample)
{
    if (sample.hasForce) {
        m_forceChart->append(0, sample.forceN);
    }
    if (sample.hasResistance) {
        m_resistanceChart->append(0, sample.resistanceOhm * m_resistanceDisplayScale / 1000000.0);
    }
    if (sample.hasVoltage) {
        m_resistanceChart->append(1, sample.voltageMv / 1000.0);
    }
}

void Widget::updateCalibrationFromUi()
{
    bool ok = false;

    double value = m_refEdit->text().toDouble(&ok);
    if (ok && value > 0.0) {
        m_refOhm = value * 1000000.0;
        m_settings->setValue("calibration/refMohm", value);
    }

    value = m_vccEdit->text().toDouble(&ok);
    if (ok && value > 0.0) {
        m_vccMv = value;
        m_settings->setValue("calibration/vccMv", value);
    }

    value = m_scaleEdit->text().toDouble(&ok);
    if (ok) {
        m_forceScaleNPerMohm = value;
        m_settings->setValue("calibration/scale", value);
    }
}

// =====================================================================
// 工具函数
// =====================================================================

int Widget::simulationIntervalMs() const
{
    return qMax(10, 120 / qMax(1, m_simSpeedSlider->value()));
}

double Widget::resistanceFromVoltage(double voltageMv) const
{
    if (voltageMv <= 0.0 || voltageMv >= m_vccMv) {
        return 0.0;
    }
    return m_refOhm * voltageMv / (m_vccMv - voltageMv);
}

double Widget::forceFromResistance(double resistanceOhm) const
{
    if (!m_baselineReady) return 0.0;

    const double R0 = m_adaptiveBaselineInit ? m_adaptiveR0 : m_baselineR0;
    const double dR = R0 - resistanceOhm;

    if (dR <= 0.0) return 0.0;

    // Piecewise model: linear zone + power law, normalized by R0
    // Ensures F(0)=0, continuous derivative, monotonic increasing
    const double x = dR / R0;            // normalized resistance drop
    const double xLinear = 0.0125;       // dR ≈ 2.5Ω for R0=200
    const double fLinear = 2.0;

    if (x <= xLinear) {
        return (fLinear / xLinear) * x;  // linear from origin
    }

    // Power law: F = c * x^1.5, matched at xLinear
    const double c = fLinear / std::pow(xLinear, 1.5);
    return c * std::pow(x, 1.5);
}

QString Widget::formatResistance(double ohm) const
{
    const double displayOhm = ohm * m_resistanceDisplayScale;
    if (displayOhm >= 1000000.0) {
        return QString("%1 MΩ").arg(displayOhm / 1000000.0, 0, 'f', 3);
    }
    if (displayOhm >= 1000.0) {
        return QString("%1 kΩ").arg(displayOhm / 1000.0, 0, 'f', 2);
    }
    return QString("%1 Ω").arg(displayOhm, 0, 'f', 1);
}

QString Widget::formatForce(double forceN) const
{
    return QString("%1 N").arg(forceN, 0, 'f', 3);
}

QString Widget::localIpHint() const
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol
            && !address.isLoopback()
            && !address.toString().startsWith("169.254.")) {
            return address.toString();
        }
    }
    return "127.0.0.1";
}
