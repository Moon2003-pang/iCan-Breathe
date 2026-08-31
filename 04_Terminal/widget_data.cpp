// widget_data.cpp — 数据采集 / 呼吸分析 / CSV 持久化 / 健康报告数据
// 从 widget.cpp 拆分（Task-R1/R3 + 采集 + 分析）

#include "widget.h"
#include "healthreportdialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTextStream>

#include <cmath>

// =====================================================================
// 数据采集控制
// =====================================================================

void Widget::toggleCollect()
{
    m_collecting = !m_collecting;
    if (m_collecting) {
        m_collectButton->setText(QString::fromUtf8("停止采集"));
        m_collectStatus->setText(QString::fromUtf8("状态: 采集中... (%1 条)").arg(m_collectedData.size()));
    } else {
        m_collectButton->setText(QString::fromUtf8("开始采集"));
        m_collectStatus->setText(QString::fromUtf8("状态: 已采集 %1 条").arg(m_collectedData.size()));
    }
}

void Widget::saveCollectedData()
{
    if (m_collectedData.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("无数据"),
                                 QString::fromUtf8("没有采集到数据。请先点击\"开始采集\"。"));
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString defaultName = QString("collect_%1.csv").arg(timestamp);

#ifdef Q_OS_ANDROID
    const QString dir = QCoreApplication::applicationDirPath();
    const QString filePath = dir + "/" + defaultName;
#else
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QString::fromUtf8("保存采集数据"),
        defaultName,
        QString::fromUtf8("CSV 文件 (*.csv)")
    );
    if (filePath.isEmpty()) return;
#endif

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QString::fromUtf8("保存失败"),
                              QString::fromUtf8("无法写入文件:\n%1").arg(filePath));
        return;
    }

    QTextStream out(&file);
    out << "timestamp,adc,voltageMv,resistanceOhm,forceN\n";
    for (const auto &pair : m_collectedData) {
        const SensorSample &s = pair.first;
        out << s.timestampMs << ","
            << s.adc << ","
            << s.voltageMv << ","
            << s.resistanceOhm << ","
            << pair.second << "\n";
    }
    file.close();

    const int savedCount = m_collectedData.size();
    m_collectedData.clear();
    m_collectStatus->setText(QString::fromUtf8("状态: 已保存至 %1，已清空").arg(QFileInfo(filePath).fileName()));

    QMessageBox::information(this, QString::fromUtf8("保存成功"),
                             QString::fromUtf8("已保存 %1 条数据到:\n%2")
                                 .arg(savedCount)
                                 .arg(filePath));
}

// =====================================================================
// 校准触发
// =====================================================================

void Widget::triggerCalibrate()
{
    m_baselineReady = false;
    m_baselineStartMs = 0;
    m_baselineR0 = 0.0;
    m_baselineBuffer.clear();
    m_adaptiveR0 = 0.0;
    m_adaptiveBaselineInit = false;
    if (m_baselineStatus) {
        m_baselineStatus->setText(QString::fromUtf8("零点: 等待数据..."));
    }
}

// =====================================================================
// 本地 DSP 呼吸分析（过零检测）
// =====================================================================

void Widget::analyzeBreath()
{
    if (!m_breathStatusLabel) return;

    static constexpr int kMinSamples = 50;
    if (m_breathBuffer.size() < kMinSamples) {
        m_breathStatusLabel->setText(QString::fromUtf8("呼吸状态: 收集中... (%1/50)")
                                    .arg(m_breathBuffer.size()));
        return;
    }

    const int n = m_breathBuffer.size();
    QList<double> detrended;
    detrended.reserve(n);
    const int maWindow = qMin(30, n / 3);
    for (int i = 0; i < n; ++i) {
        int wStart = qMax(0, i - maWindow / 2);
        int wEnd = qMin(n, i + maWindow / 2);
        double sum = 0.0;
        for (int j = wStart; j < wEnd; ++j) sum += m_breathBuffer.at(j);
        double ma = sum / (wEnd - wStart);
        detrended.append(m_breathBuffer.at(i) - ma);
    }

    double dMin = detrended[0], dMax = detrended[0];
    for (double v : detrended) {
        if (v < dMin) dMin = v;
        if (v > dMax) dMax = v;
    }
    const double amplitude = dMax - dMin;

    QList<int> zeroCrossIdx;
    for (int i = 1; i < n; ++i) {
        if (detrended.at(i - 1) <= 0.0 && detrended.at(i) > 0.0) {
            zeroCrossIdx.append(i);
        }
    }

    if (zeroCrossIdx.size() < 2) {
        if (amplitude < 0.05) {
            m_breathStatusLabel->setText(QString::fromUtf8("呼吸状态: 静止"));
        } else {
            m_breathStatusLabel->setText(QString::fromUtf8("呼吸状态: 检测中..."));
        }
        return;
    }

    const double durationSec = static_cast<double>(zeroCrossIdx.last() - zeroCrossIdx.first()) / 5.0;
    const int cycles = zeroCrossIdx.size() - 1;
    double breathRate = 0.0;
    if (durationSec > 0.0) {
        breathRate = static_cast<double>(cycles) / durationSec * 60.0;
    }

    double sumIntv = 0.0;
    QList<double> intervals;
    for (int i = 1; i < zeroCrossIdx.size(); ++i) {
        double iv = zeroCrossIdx.at(i) - zeroCrossIdx.at(i - 1);
        intervals.append(iv);
        sumIntv += iv;
    }
    double meanIntv = sumIntv / intervals.size();
    double varIntv = 0.0;
    for (double iv : intervals) {
        varIntv += (iv - meanIntv) * (iv - meanIntv);
    }
    varIntv /= intervals.size();
    double cv = meanIntv > 0.0 ? sqrt(varIntv) / meanIntv : 0.0;

    QString status;
    if (amplitude < 0.05) {
        status = QString::fromUtf8("呼吸状态: 屏气 (本地DSP)");
    } else if (breathRate > 30.0) {
        status = QString::fromUtf8("呼吸状态: 呼吸过快 (本地DSP, %1 次/分)").arg(breathRate, 0, 'f', 0);
    } else if (breathRate < 6.0) {
        status = QString::fromUtf8("呼吸状态: 呼吸过慢 (本地DSP, %1 次/分)").arg(breathRate, 0, 'f', 0);
    } else if (cv > 0.5) {
        status = QString::fromUtf8("呼吸状态: 呼吸异常 (本地DSP, %1 次/分)").arg(breathRate, 0, 'f', 0);
    } else {
        status = QString::fromUtf8("呼吸状态: 呼吸正常 (本地DSP, %1 次/分)").arg(breathRate, 0, 'f', 0);
    }

    m_breathStatusLabel->setText(status);
}

// =====================================================================
// Task-R1: 呼吸历史 CSV 持久化
// =====================================================================

void Widget::initBreathHistory()
{
    const QString csvPath = QCoreApplication::applicationDirPath() + "/breath_history.csv";
    bool needHeader = !QFileInfo::exists(csvPath);

    m_breathHistoryFile = new QFile(csvPath, this);
    if (!m_breathHistoryFile->open(QIODevice::Append | QIODevice::Text)) {
        qDebug() << "[CSV] Failed to open breath_history.csv";
        delete m_breathHistoryFile;
        m_breathHistoryFile = nullptr;
        return;
    }

    m_breathHistoryStream = new QTextStream(m_breathHistoryFile);
    if (needHeader) {
        *m_breathHistoryStream << "timestamp_ms,resistance_ohm,voltage_mv,force_n,breath_status,anomaly_score,alert\n";
        m_breathHistoryStream->flush();
    }
    qDebug() << "[CSV] Breath history initialized:" << csvPath;
}

void Widget::flushCsvBuffer()
{
    if (m_csvWriteBuffer.isEmpty()) return;
    if (!m_breathHistoryStream || !m_breathHistoryFile) return;

    if (m_breathHistoryFile->size() > kCsvMaxFileSize) {
        rotateCsvFile();
    }

    for (const auto &s : m_csvWriteBuffer) {
        *m_breathHistoryStream << s.timestampMs << ","
            << QString::number(s.resistanceOhm, 'f', 1) << ","
            << QString::number(s.voltageMv, 'f', 1) << ","
            << QString::number(s.forceN, 'f', 3) << ","
            << (s.breathStatus.isEmpty() ? "N/A" : s.breathStatus) << ","
            << QString::number(s.anomalyScore, 'f', 3) << ","
            << s.alert << "\n";
    }
    m_breathHistoryStream->flush();
    m_csvWriteBuffer.clear();
}

void Widget::rotateCsvFile()
{
    if (!m_breathHistoryFile || !m_breathHistoryStream) return;

    m_breathHistoryStream->flush();
    m_breathHistoryStream->flush();
    delete m_breathHistoryStream;
    m_breathHistoryStream = nullptr;
    m_breathHistoryFile->close();

    const QString oldPath = m_breathHistoryFile->fileName();
    const QString newPath = QCoreApplication::applicationDirPath()
        + QString("/breath_history_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));
    QFile::remove(newPath);
    QFile::rename(oldPath, newPath);
    qDebug() << "[CSV] Rotated to" << newPath;

    delete m_breathHistoryFile;
    m_breathHistoryFile = new QFile(oldPath, this);
    if (m_breathHistoryFile->open(QIODevice::Append | QIODevice::Text)) {
        m_breathHistoryStream = new QTextStream(m_breathHistoryFile);
        *m_breathHistoryStream << "timestamp_ms,resistance_ohm,voltage_mv,force_n,breath_status,anomaly_score,alert\n";
        m_breathHistoryStream->flush();
    } else {
        qDebug() << "[CSV] Failed to create new file after rotation";
        m_breathHistoryFile = nullptr;
    }
}

// =====================================================================
// 历史数据读取与统计
// =====================================================================

BreathReportData Widget::loadRecentBreathData(int minutes)
{
    BreathReportData result;

    QList<SensorSample> memSamples;
    qint64 cutoffMs = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(minutes) * 60 * 1000;

    for (int i = m_csvWriteBuffer.size() - 1; i >= 0; --i) {
        if (m_csvWriteBuffer[i].timestampMs >= cutoffMs) {
            memSamples.prepend(m_csvWriteBuffer[i]);
        } else break;
    }

    QList<SensorSample> fileSamples;
    const QString csvPath = QCoreApplication::applicationDirPath() + "/breath_history.csv";
    QFile file(csvPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString header = in.readLine();
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList fields = line.split(',');
            if (fields.size() < 7) continue;

            SensorSample s;
            bool ok = false;
            s.timestampMs = fields[0].toLongLong(&ok);
            if (!ok || s.timestampMs < cutoffMs) continue;

            s.resistanceOhm = fields[1].toDouble();
            s.voltageMv = fields[2].toDouble();
            s.forceN = fields[3].toDouble();
            s.breathStatus = fields[4];
            s.anomalyScore = fields[5].toDouble();
            s.alert = fields[6].toInt();
            s.hasResistance = s.resistanceOhm > 0;
            s.hasVoltage = s.voltageMv > 0;
            s.hasForce = s.forceN != 0;

            fileSamples.append(s);
        }
    }

    result.samples = fileSamples + memSamples;

    if (!result.samples.isEmpty()) {
        BreathSummary &sum = result.summary;
        sum.totalSamples = result.samples.size();
        sum.durationSec = (result.samples.last().timestampMs - result.samples.first().timestampMs) / 1000.0;
        if (sum.durationSec <= 0) sum.durationSec = sum.totalSamples / 5.0;

        int cntNormal = 0, cntAbnormal = 0, cntTachy = 0, cntBrady = 0, cntApnea = 0;
        double scoreSum = 0, scoreMax = 0;
        QList<double> scores;
        int alertCnt = 0;
        qint64 alertDurMs = 0;
        double peakForce = 0;
        qint64 alertStartMs = 0;
        bool inAlert = false;

        for (const auto &s : result.samples) {
            if (s.breathStatus == QString::fromUtf8("呼吸正常")) cntNormal++;
            else if (s.breathStatus == QString::fromUtf8("呼吸异常")) cntAbnormal++;
            else if (s.breathStatus == QString::fromUtf8("呼吸过快")) cntTachy++;
            else if (s.breathStatus == QString::fromUtf8("呼吸过慢")) cntBrady++;
            else if (s.breathStatus == QString::fromUtf8("屏气")) cntApnea++;

            scoreSum += s.anomalyScore;
            scores.append(s.anomalyScore);
            if (s.anomalyScore > scoreMax) scoreMax = s.anomalyScore;

            if (s.alert == 1 && !inAlert) {
                alertCnt++;
                alertStartMs = s.timestampMs;
                inAlert = true;
            } else if (s.alert == 0 && inAlert) {
                alertDurMs += s.timestampMs - alertStartMs;
                inAlert = false;
            }
            if (inAlert && s.forceN > peakForce) peakForce = s.forceN;
            if (!inAlert && s.forceN > peakForce) peakForce = s.forceN;
        }

        double total = qMax(1, sum.totalSamples);
        sum.normalPct = cntNormal / total * 100.0;
        sum.abnormalPct = cntAbnormal / total * 100.0;
        sum.tachypneaPct = cntTachy / total * 100.0;
        sum.bradypneaPct = cntBrady / total * 100.0;
        sum.apneaPct = cntApnea / total * 100.0;
        sum.anomalyScoreMean = scoreSum / total;
        sum.anomalyScoreMax = scoreMax;

        double varSum = 0;
        for (double sc : scores) varSum += (sc - sum.anomalyScoreMean) * (sc - sum.anomalyScoreMean);
        sum.anomalyScoreStd = sqrt(varSum / total);

        sum.alertCount = alertCnt;
        sum.alertDurationMs = alertDurMs;
        sum.peakForceN = peakForce;
    }

    return result;
}

BreathSummary Widget::loadHistoricalSummary(int minutes)
{
    return loadRecentBreathData(minutes).summary;
}

// =====================================================================
// Task-R3: 移动端健康报告摘要
// =====================================================================

void Widget::updateHealthReportSummary()
{
    if (!m_healthReportSummary) return;

    BreathReportData data = loadRecentBreathData(5);
    if (data.samples.isEmpty()) {
        m_healthReportSummary->setText(QString::fromUtf8("暂无数据，请先采集呼吸数据"));
        m_healthReportSummary->setStyleSheet("font-size:14px; color:#A0A0A0; padding:4px 0;");
        return;
    }

    const BreathSummary &s = data.summary;
    int score = static_cast<int>(s.normalPct * 0.7);
    int anomalyPenalty = qBound(0, static_cast<int>(s.anomalyScoreMean * 10), 30);
    score = qBound(0, score + (30 - anomalyPenalty), 100);

    QString summaryText;
    QString color;
    if (score >= 80) {
        summaryText = QString::fromUtf8("评分 %1/100 - 呼吸状态整体良好").arg(score);
        color = "#4CAF50";
    } else if (score >= 60) {
        summaryText = QString::fromUtf8("评分 %1/100 - 呼吸状态基本正常，有少量异常").arg(score);
        color = "#FFB74D";
    } else if (score >= 40) {
        summaryText = QString::fromUtf8("评分 %1/100 - 呼吸状态需关注").arg(score);
        color = "#FF9800";
    } else {
        summaryText = QString::fromUtf8("评分 %1/100 - 呼吸状态较差").arg(score);
        color = "#FF5252";
    }

    m_healthReportSummary->setText(summaryText);
    m_healthReportSummary->setStyleSheet(
        QString("font-size:14px; font-weight:700; color:%1; padding:4px 0;").arg(color));
}

void Widget::openHealthReport()
{
    auto *dialog = new HealthReportDialog(this, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
