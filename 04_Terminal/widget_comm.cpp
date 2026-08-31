// widget_comm.cpp — TCP 通信 & JSON 解析 & 采样入库
// 从 widget.cpp 拆分

#include "widget.h"
#include "healthreportdialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <cmath>

namespace {
constexpr int kMaxTcpLineBytes = 4096;

double jsonNumber(const QJsonObject &obj, const QStringList &keys, bool *ok)
{
    for (const QString &key : keys) {
        const QJsonValue value = obj.value(key);
        if (value.isDouble()) {
            *ok = true;
            return value.toDouble();
        }
        if (value.isString()) {
            bool converted = false;
            const double number = value.toString().toDouble(&converted);
            if (converted) {
                *ok = true;
                return number;
            }
        }
    }

    *ok = false;
    return 0.0;
}
}

// =====================================================================
// TCP Server 生命周期
// =====================================================================

void Widget::startServer()
{
    updateCalibrationFromUi();
    m_stoppingServer = false;

    bool ok = false;
    const quint16 port = m_portEdit->text().toUShort(&ok);
    if (!ok || port == 0) {
        QMessageBox::warning(this, QString::fromUtf8("端口错误"),
                             QString::fromUtf8("请输入有效端口号。"));
        return;
    }

    if (!m_server->listen(QHostAddress::Any, port)) {
        QMessageBox::critical(this, QString::fromUtf8("监听失败"), m_server->errorString());
        return;
    }

    m_settings->setValue("server/port", port);
    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_portEdit->setEnabled(false);
    updateConnectionState();
}

void Widget::stopServer()
{
    if (m_stoppingServer) {
        return;
    }

    m_stoppingServer = true;
    m_stopButton->setEnabled(false);

    if (m_server) {
        if (m_server->isListening()) {
            m_server->close();
        }

        while (m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
    }

    if (m_client) {
        QTcpSocket *socket = m_client;
        m_client = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    m_rxBuffer.clear();
    m_startButton->setEnabled(true);
    m_portEdit->setEnabled(true);
    m_stoppingServer = false;
    updateConnectionState();
}

void Widget::acceptClient()
{
    if (!m_server) {
        return;
    }

    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (m_stoppingServer || !m_server->isListening()) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
            continue;
        }

        if (m_client) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
            continue;
        }

        m_client = socket;
        m_rxBuffer.clear();
        connect(m_client, &QTcpSocket::readyRead, this, &Widget::readClientData);
        connect(m_client, &QTcpSocket::disconnected, this, &Widget::clientDisconnected);
        startWatchdog();
        updateConnectionState();
    }
}

void Widget::clientDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (m_stoppingServer) {
        if (socket && socket != m_client) {
            socket->deleteLater();
        }
        return;
    }

    if (socket && socket != m_client) {
        socket->deleteLater();
        return;
    }

    if (m_client) {
        socket = m_client;
        m_client = nullptr;
        socket->disconnect(this);
        socket->deleteLater();
    }

    m_rxBuffer.clear();

    m_baselineReady = false;
    m_baselineStartMs = 0;
    m_baselineR0 = 0.0;
    m_adaptiveR0 = 0.0;
    m_adaptiveBaselineInit = false;

    stopWatchdog();

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

    updateConnectionState();
}

// =====================================================================
// 数据读取与 JSON 解析
// =====================================================================

void Widget::readClientData()
{
    if (m_stoppingServer) {
        return;
    }

    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        socket = m_client;
    }
    if (!socket || socket != m_client) {
        return;
    }

    m_rxBuffer.append(socket->readAll());
    while (true) {
        const int newlineIndex = m_rxBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            if (m_rxBuffer.size() > kMaxTcpLineBytes) {
                m_rxBuffer.clear();
            }
            return;
        }

        QByteArray line = m_rxBuffer.left(newlineIndex).trimmed();
        m_rxBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            parseIncomingLine(line);
            resetWatchdog();
        }
    }
}

void Widget::parseIncomingLine(const QByteArray &line)
{
    SensorSample sample;
    if (!parseJsonSample(line, &sample)) {
        return;
    }
    enrichSample(&sample);
    appendSample(sample);
}

bool Widget::parseJsonSample(const QByteArray &line, SensorSample *sample) const
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    bool ok = false;

    sample->timestampMs = QDateTime::currentMSecsSinceEpoch();

    double value = jsonNumber(obj, {"adc", "raw", "adcRaw"}, &ok);
    if (ok) {
        sample->adc = value;
        sample->hasAdc = true;
    }

    value = jsonNumber(obj, {"voltageMv", "mv", "millivolt"}, &ok);
    if (ok) {
        sample->voltageMv = value;
        sample->hasVoltage = true;
    } else {
        value = jsonNumber(obj, {"voltage", "v"}, &ok);
        if (ok) {
            sample->voltageMv = value * 1000.0;
            sample->hasVoltage = true;
        }
    }

    value = jsonNumber(obj, {"resistance", "resistanceOhm", "ohm", "r"}, &ok);
    if (ok) {
        sample->resistanceOhm = value;
        sample->hasResistance = true;
    } else {
        value = jsonNumber(obj, {"resistanceMohm", "mohm"}, &ok);
        if (ok) {
            sample->resistanceOhm = value * 1000000.0;
            sample->hasResistance = true;
        }
    }

    value = jsonNumber(obj, {"force", "forceN", "n"}, &ok);
    if (ok) {
        sample->forceN = value;
        sample->hasForce = true;
    }

    if (obj.contains("breathStatus")) {
        sample->breathStatus = obj.value("breathStatus").toString();
    }
    value = jsonNumber(obj, {"anomalyScore", "breathScore"}, &ok);
    if (ok) {
        sample->anomalyScore = value;
    }

    value = jsonNumber(obj, {"alert"}, &ok);
    if (ok) {
        sample->alert = static_cast<int>(value);
    }

    return sample->hasResistance || sample->hasVoltage || sample->hasAdc || sample->hasForce;
}

void Widget::enrichSample(SensorSample *sample) const
{
    if (!sample->hasVoltage && sample->hasAdc) {
        sample->voltageMv = sample->adc / 4095.0 * m_vccMv;
        sample->hasVoltage = true;
    }

    if (!sample->hasResistance && sample->hasVoltage) {
        sample->resistanceOhm = resistanceFromVoltage(sample->voltageMv);
        sample->hasResistance = sample->resistanceOhm > 0.0;
    }

    if (!sample->hasForce && sample->hasResistance) {
        sample->forceN = forceFromResistance(sample->resistanceOhm);
        sample->hasForce = true;
    }
}

// =====================================================================
// 采样入库 + 触发报警/基线/波形
// =====================================================================

void Widget::appendSample(const SensorSample &sample)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastSampleWallMs > 0) {
        const qint64 dt = qMax<qint64>(1, now - m_lastSampleWallMs);
        m_lastRateHz = 1000.0 / static_cast<double>(dt);
    }
    m_lastSampleWallMs = now;

    m_current = sample;
    ++m_sampleCount;

    const bool breathMode = m_breathModeCheck && m_breathModeCheck->isChecked();
    if (m_collecting) {
        double forceN = 0.0;
        if (breathMode) {
            forceN = forceFromResistance(sample.resistanceOhm);
        } else {
            bool ok = false;
            forceN = m_forceInput->text().toDouble(&ok);
            if (!ok) forceN = 0.0;
        }
        SensorSample recorded = sample;
        recorded.forceN = forceN;
        recorded.hasForce = true;
        m_collectedData.append(qMakePair(recorded, forceN));
        m_collectStatus->setText(
            QString::fromUtf8("状态: 已采集 %1 条").arg(m_collectedData.size()));
    }

    m_csvWriteBuffer.append(sample);
    if (m_csvWriteBuffer.size() >= kCsvFlushInterval) {
        flushCsvBuffer();
    }

    if (!sample.breathStatus.isEmpty() && m_breathStatusLabel) {
        QString statusText = QString::fromUtf8("呼吸状态: %1 (AI, 分数=%2)")
            .arg(sample.breathStatus)
            .arg(sample.anomalyScore, 0, 'f', 2);
        m_breathStatusLabel->setText(statusText);
    } else if (sample.hasResistance && sample.resistanceOhm > 0.0) {
        m_breathBuffer.append(sample.resistanceOhm);
        while (m_breathBuffer.size() > kBreathWindowSamples) {
            m_breathBuffer.removeFirst();
        }
        analyzeBreath();
    }

    if (sample.alert == 1 && !m_alertActive) {
        m_alertActive = true;
        m_alertVisible = true;
        showAlarm();
        if (!m_emergencySent) {
            m_emergencySent = true;
            sendEmergencyMessage(
                QString::fromUtf8("按压报警触发 | 力值=%1N | 电阻=%2Ω")
                    .arg(sample.forceN, 0, 'f', 2)
                    .arg(sample.resistanceOhm, 0, 'f', 1));
        }
        logAlertEvent("TRIGGER");
        m_alertTriggerTimeMs = QDateTime::currentMSecsSinceEpoch();
        m_alertPeakForceN = sample.forceN;
        m_reminderActive = false;
        m_reminderTimer->start();
    } else if (sample.alert == 0 && m_alertActive) {
        m_alertActive = false;
        m_emergencySent = false;
        hideAlarm();
        logAlertEvent("CLEARED", "esp32");
        m_reminderTimer->stop();
        if (m_reminderActive) {
            m_reminderActive = false;
            m_alertBlinkTimer->setInterval(500);
        }
    } else if (m_alertActive) {
        updateAlertPeakForce(sample.forceN);
    }

    if (!m_baselineReady && sample.hasResistance && sample.resistanceOhm > 0.0) {
        if (m_baselineStartMs == 0) {
            m_baselineStartMs = now;
            m_baselineBuffer.clear();
        }
        m_baselineBuffer.append(sample.resistanceOhm);

        qint64 elapsed = now - m_baselineStartMs;
        if (elapsed >= kBaselineDurationMs && m_baselineBuffer.size() > 0) {
            double sum = 0.0;
            for (double r : m_baselineBuffer) sum += r;
            m_baselineR0 = sum / m_baselineBuffer.size();
            m_adaptiveR0 = m_baselineR0;
            m_adaptiveBaselineInit = true;
            m_baselineReady = true;
            m_baselineBuffer.clear();
            if (m_zeroEdit) {
                double displayMohm = m_baselineR0 * m_resistanceDisplayScale / 1000000.0;
                m_zeroEdit->setText(QString::number(displayMohm, 'f', 3));
            }
        }
    }

    // Adaptive baseline: track slow sensor drift (temperature, humidity, aging)
    if (m_adaptiveBaselineInit && sample.hasResistance && sample.resistanceOhm > 0.0) {
        double delta = sample.resistanceOhm - m_adaptiveR0;
        double guardOhm = m_adaptiveR0 * kAdaptiveGuardRatio;  // 5% of R0
        if (qAbs(delta) < guardOhm) {
            m_adaptiveR0 += kAdaptiveAlpha * delta;
        }
    }

    updateCharts(sample);
    updateMetrics();
}
