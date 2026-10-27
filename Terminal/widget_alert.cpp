// widget_alert.cpp — 报警 UI / 阈值下发 / 紧急联系人 / 看门狗 / 日志 / 二次提醒
// 从 widget.cpp 拆分（Task-D/E/F/G/H/I）

#include "widget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>

// =====================================================================
// Task-D: 报警 UI 显示 / 手动解除
// =====================================================================

void Widget::showAlarm()
{
    if (m_alertLabel) {
        m_alertLabel->setText(QString::fromUtf8("⚠ 异常按压！"));
        m_alertLabel->setVisible(true);
    }
    if (m_clearAlertButton) {
        m_clearAlertButton->setEnabled(true);
    }
    m_alertBlinkTimer->start();
    this->setStyleSheet(this->styleSheet());
}

void Widget::hideAlarm()
{
    m_alertBlinkTimer->stop();
    if (m_alertLabel) {
        m_alertLabel->setVisible(false);
    }
    if (m_clearAlertButton) {
        m_clearAlertButton->setEnabled(false);
    }
    m_alertVisible = false;
}

void Widget::toggleAlertBlink()
{
    if (!m_alertActive || !m_alertLabel) return;
    m_alertVisible = !m_alertVisible;
    m_alertLabel->setVisible(m_alertVisible);
}

void Widget::sendClearAlert()
{
    if (m_client && m_client->isWritable()) {
        m_client->write("{\"cmd\":\"clear_alert\"}\n");
        qDebug() << "[ALERT] Sent clear_alert command to ESP32";
    }
}

// =====================================================================
// Task-E: 报警阈值远程配置
// =====================================================================

void Widget::sendThresholdConfig()
{
    if (!m_client || !m_client->isWritable()) return;

    bool ok1 = false, ok2 = false;
    double forceThreshold = m_thresholdForceEdit->text().toDouble(&ok1);
    double holdoff = m_thresholdHoldoffEdit->text().toDouble(&ok2);
    if (!ok1 || !ok2) {
        QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                             QString::fromUtf8("请输入有效的数字。"));
        return;
    }

    QJsonObject forceModel;
    forceModel["a"] = 1.4086;
    forceModel["b"] = 0.0900;

    QJsonObject cmd;
    cmd["cmd"] = "set_threshold";
    cmd["forceThresholdN"] = forceThreshold;
    cmd["holdoffSec"] = holdoff;
    cmd["forceModel"] = forceModel;

    QByteArray data = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
    m_client->write(data);
    qDebug() << "[THRESHOLD] Sent config:" << data;

    m_settings->setValue("alert/forceThresholdN", forceThreshold);
    m_settings->setValue("alert/holdoffSec", holdoff);
}

void Widget::updateThresholdButtonState()
{
    if (m_sendThresholdButton) {
        m_sendThresholdButton->setEnabled(m_client != nullptr);
    }
}

// =====================================================================
// Task-F: 紧急联系人通知
// =====================================================================

void Widget::loadEmergencyContacts()
{
    QStringList searchPaths;
    searchPaths << QCoreApplication::applicationDirPath() + "/emergency_contacts.json"
                << "emergency_contacts.json"
                << "07_terminal/emergency_contacts.json";

    for (const QString &path : searchPaths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
            file.close();
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                m_emergencyContacts = doc.object().value("contacts").toArray();
                qDebug() << "[EMERGENCY] Loaded" << m_emergencyContacts.size()
                         << "contacts from" << path;
                return;
            }
        }
    }
    qDebug() << "[EMERGENCY] No contacts file found, emergency notification disabled";
}

void Widget::sendEmergencyMessage(const QString &alertInfo)
{
    qDebug() << "[EMERGENCY] Alert triggered, would notify:"
             << m_emergencyContacts.size() << "contacts"
             << "| Info:" << alertInfo;

    for (int i = 0; i < m_emergencyContacts.size(); ++i) {
        QJsonObject contact = m_emergencyContacts.at(i).toObject();
        qDebug() << "[EMERGENCY]   Contact" << i + 1 << ":"
                 << contact.value("name").toString()
                 << contact.value("phone").toString()
                 << contact.value("email").toString();
    }
}

// =====================================================================
// Task-G: 连接看门狗
// =====================================================================

void Widget::startWatchdog()
{
    m_watchdogAlerted = false;
    if (m_watchdogTimer) {
        m_watchdogTimer->start();
    }
    if (m_watchdogStatus) {
        m_watchdogStatus->setVisible(false);
    }
}

void Widget::stopWatchdog()
{
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }
    m_watchdogAlerted = false;
    if (m_watchdogStatus) {
        m_watchdogStatus->setVisible(false);
    }
}

void Widget::resetWatchdog()
{
    if (m_watchdogTimer && m_client) {
        m_watchdogTimer->start();
        if (m_watchdogAlerted) {
            m_watchdogAlerted = false;
            if (m_watchdogStatus) {
                m_watchdogStatus->setVisible(false);
            }
            qDebug() << "[WATCHDOG] Connection recovered";
        }
    }
}

void Widget::onWatchdogTimeout()
{
    if (!m_client) return;
    m_watchdogAlerted = true;
    if (m_watchdogStatus) {
        m_watchdogStatus->setText(QString::fromUtf8("⚠ 设备离线 / 连接中断（>5秒无数据）"));
        m_watchdogStatus->setVisible(true);
    }
    qDebug() << "[WATCHDOG] No data received for 5 seconds, device may be offline";
}

// =====================================================================
// Task-H: 报警历史日志
// =====================================================================

void Widget::logAlertEvent(const QString &event, const QString &clearMethod)
{
    const QString csvPath = QCoreApplication::applicationDirPath() + "/alert_history.csv";
    bool needHeader = !QFileInfo::exists(csvPath);

    QFile file(csvPath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        qDebug() << "[HISTORY] Failed to open" << csvPath << "for writing";
        return;
    }

    QTextStream out(&file);
    if (needHeader) {
        out << "event,timestamp,trigger_time_ms,duration_ms,peak_force_n,clear_method\n";
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (event == "TRIGGER") {
        out << "TRIGGER," << timestamp << "," << now << ",0,0.0,\n";
    } else if (event == "CLEARED") {
        qint64 duration = (m_alertTriggerTimeMs > 0) ? (now - m_alertTriggerTimeMs) : 0;
        out << "CLEARED," << timestamp << "," << m_alertTriggerTimeMs << ","
            << duration << "," << QString::number(m_alertPeakForceN, 'f', 2) << ","
            << clearMethod << "\n";
    }

    file.close();
    qDebug() << "[HISTORY] Logged" << event << "to" << csvPath;
}

void Widget::updateAlertPeakForce(double forceN)
{
    if (qAbs(forceN) > m_alertPeakForceN) {
        m_alertPeakForceN = qAbs(forceN);
    }
}

// =====================================================================
// Task-I: 30秒二次提醒
// =====================================================================

void Widget::onReminderTimeout()
{
    if (!m_alertActive) return;
    m_reminderActive = true;
    m_alertBlinkTimer->setInterval(200);
    qDebug() << "[REMINDER] Alert uncleared for 30s, intensifying notification";
}
