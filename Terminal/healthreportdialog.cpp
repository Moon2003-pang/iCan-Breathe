#include "healthreportdialog.h"
#include "widget.h"
#include "realtimechart.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>

// ────────────────────────────────────────────────────────
// Construction
// ────────────────────────────────────────────────────────

HealthReportDialog::HealthReportDialog(Widget *mainWidget, QWidget *parent)
    : QDialog(parent)
    , m_mainWidget(mainWidget)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QString::fromUtf8("呼吸健康报告"));
#ifdef Q_OS_ANDROID
    showMaximized();
#else
    resize(820, 640);
#endif
    setupUi();

    connect(m_nam, &QNetworkAccessManager::finished,
            this, &HealthReportDialog::onApiReply);
}

// ────────────────────────────────────────────────────────
// UI Setup
// ────────────────────────────────────────────────────────

void HealthReportDialog::setupUi()
{
    // Build all content into a single widget
    auto *contentWidget = new QWidget();
    contentWidget->setObjectName("reportContent");

    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(14);

    // --- Header ---
    auto *headerRow = new QHBoxLayout();
    auto *title = new QLabel(QString::fromUtf8("呼吸健康报告"), contentWidget);
    title->setStyleSheet("font-size:22px;font-weight:800;color:#F0F0F0;");
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    auto *ts = new QLabel(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"), contentWidget);
    ts->setStyleSheet("color:#A0A0A0;font-size:13px;");
    headerRow->addWidget(ts);
    contentLayout->addLayout(headerRow);

    // --- Score card ---
    auto *scoreFrame = new QFrame(contentWidget);
    scoreFrame->setObjectName("scoreCard");
    scoreFrame->setStyleSheet(
        "QFrame#scoreCard { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #1a2233, stop:1 #0f141d); border:1px solid #0078D4;"
        "border-radius:4px; }");
    auto *scoreLayout = new QVBoxLayout(scoreFrame);
    scoreLayout->setContentsMargins(24, 18, 24, 18);

    m_scoreValue = new QLabel(QString::fromUtf8("-- / 100"), scoreFrame);
    m_scoreValue->setObjectName("scoreValue");
    m_scoreValue->setAlignment(Qt::AlignCenter);
    m_scoreValue->setStyleSheet("font-size:42px;font-weight:900;color:#0078D4;");
    scoreLayout->addWidget(m_scoreValue);

    m_scoreSummary = new QLabel(QString::fromUtf8("点击\"生成报告\"开始分析"), scoreFrame);
    m_scoreSummary->setAlignment(Qt::AlignCenter);
    m_scoreSummary->setStyleSheet("font-size:15px;color:#A0A0A0;padding-top:4px;");
    scoreLayout->addWidget(m_scoreSummary);

    contentLayout->addWidget(scoreFrame);

    // --- Charts row: trend + status distribution ---
    auto *chartsRow = new QHBoxLayout();
    chartsRow->setSpacing(12);

    // Trend chart
    auto *trendGroup = new QGroupBox(QString::fromUtf8("呼吸频率趋势"), contentWidget);
    trendGroup->setObjectName("panel");
    auto *trendLayout = new QVBoxLayout(trendGroup);
    trendLayout->setContentsMargins(10, 10, 10, 10);
    m_trendChart = new RealtimeChart(trendGroup);
    m_trendChart->addSeries("BPM", QColor(0x22c55e));
    m_trendChart->setUnit("bpm");
    m_trendChart->setAutoScaleY(true);
    trendLayout->addWidget(m_trendChart, 1);
    chartsRow->addWidget(trendGroup, 3);

    // Status distribution
    auto *distGroup = new QGroupBox(QString::fromUtf8("状态分布"), contentWidget);
    distGroup->setObjectName("panel");
    auto *distLayout = new QVBoxLayout(distGroup);
    distLayout->setContentsMargins(10, 10, 10, 10);
    m_statusDistLabel = new QLabel(QString::fromUtf8("等待数据..."), distGroup);
    m_statusDistLabel->setWordWrap(true);
    m_statusDistLabel->setStyleSheet("color:#E0E0E0;font-size:13px;line-height:1.6;");
    distLayout->addWidget(m_statusDistLabel);
    chartsRow->addWidget(distGroup, 2);

    contentLayout->addLayout(chartsRow, 2);

    // --- AI Analysis (scrollable) ---
    auto *aiGroup = new QGroupBox(QString::fromUtf8("AI 分析报告"), contentWidget);
    aiGroup->setObjectName("panel");
    auto *aiLayout = new QVBoxLayout(aiGroup);
    aiLayout->setContentsMargins(10, 10, 10, 10);
    m_aiBrowser = new QTextBrowser(aiGroup);
    m_aiBrowser->setOpenExternalLinks(true);
    m_aiBrowser->setStyleSheet(
        "QTextBrowser { background:#1e293b; color:#E0E0E0; border:1px solid #3D3D3D;"
        "border-radius:4px; padding:10px; font-size:13px; }");
    m_aiBrowser->setPlaceholderText(QString::fromUtf8("AI 分析内容将在生成报告后显示..."));
    aiLayout->addWidget(m_aiBrowser, 1);
    contentLayout->addWidget(aiGroup, 3);

    // --- Loading bar ---
    auto *loadingRow = new QHBoxLayout();
    m_loadingBar = new QProgressBar(contentWidget);
    m_loadingBar->setRange(0, 0);  // indeterminate
    m_loadingBar->setFixedHeight(6);
    m_loadingBar->setVisible(false);
    m_loadingBar->setStyleSheet(
        "QProgressBar { background:#0b0f16; border:none; border-radius:3px; }"
        "QProgressBar::chunk { background:#0078D4; border-radius:3px; }");
    loadingRow->addWidget(m_loadingBar);

    m_loadingLabel = new QLabel(contentWidget);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setStyleSheet("color:#A0A0A0;font-size:12px;");
    m_loadingLabel->setVisible(false);
    loadingRow->addWidget(m_loadingLabel);
    contentLayout->addLayout(loadingRow);

    // --- Bottom buttons ---
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_generateBtn = new QPushButton(QString::fromUtf8("生成报告"), contentWidget);
    m_generateBtn->setObjectName("primaryButton");
    m_generateBtn->setStyleSheet(
        "QPushButton { background:#0078D4; color:#fff; border:none; border-radius:4px;"
        "padding:8px 22px; font-weight:700; font-size:14px; }"
        "QPushButton:hover { background:#005A9E; }"
        "QPushButton:disabled { background:#3D3D3D; color:#6D6D6D; }");
    connect(m_generateBtn, &QPushButton::clicked, this, &HealthReportDialog::generateReport);
    btnRow->addWidget(m_generateBtn);

    m_exportHtmlBtn = new QPushButton(QString::fromUtf8("导出 HTML"), contentWidget);
    m_exportHtmlBtn->setObjectName("secondaryButton");
    m_exportHtmlBtn->setStyleSheet(
        "QPushButton { background:#2D2D2D; color:#E0E0E0; border:1px solid #505050;"
        "border-radius:4px; padding:8px 18px; font-weight:600; font-size:13px; }"
        "QPushButton:hover { background:#3D3D3D; }"
        "QPushButton:disabled { color:#6D6D6D; background:#1e293b; }");
    m_exportHtmlBtn->setEnabled(false);
    connect(m_exportHtmlBtn, &QPushButton::clicked, this, &HealthReportDialog::exportHtml);
    btnRow->addWidget(m_exportHtmlBtn);

    btnRow->addStretch(1);

    auto *closeBtn = new QPushButton(QString::fromUtf8("关闭"), contentWidget);
    closeBtn->setStyleSheet(
        "QPushButton { background:#1e293b; color:#A0A0A0; border:1px solid #3D3D3D;"
        "border-radius:4px; padding:8px 18px; font-size:13px; }"
        "QPushButton:hover { background:#3D3D3D; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);

    contentLayout->addLayout(btnRow);

    // --- Wrap content in scroll area for mobile ---
#ifdef Q_OS_ANDROID
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(contentWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background:#10131a; border:none; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scrollArea);
#else
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(contentWidget);
#endif

    // Global dark background
    setStyleSheet("QDialog { background:#10131a; color:#E0E0E0; }"
                  "QWidget#reportContent { background:#10131a; color:#E0E0E0; }"
                  "QGroupBox { background:#171c26; border:1px solid #3D3D3D;"
                  "border-radius:4px; margin-top:14px; padding-top:18px; font-size:12px;"
                  "font-weight:700; color:#A0A0A0; }"
                  "QGroupBox::title { subcontrol-origin: margin; left:14px; padding:0 6px; }");
}

// ────────────────────────────────────────────────────────
// Generate report — local-first, API-enhanced optional
// ────────────────────────────────────────────────────────

void HealthReportDialog::generateReport()
{
    // Load breath report data (expand to 10 minutes for better coverage)
    BreathReportData data = m_mainWidget->loadRecentBreathData(10);
    if (data.samples.isEmpty()) {
        showError(QString::fromUtf8("没有可用的呼吸数据。\n请先连接设备并采集至少 30 秒数据。"));
        return;
    }

    // Update local visuals
    const BreathSummary &s = data.summary;

    // Score card
    int score = calculateLocalScore(s.normalPct, s.anomalyScoreMean);
    m_scoreValue->setText(QString("%1 / 100").arg(score));

    QString summaryText;
    if (score >= 80) summaryText = QString::fromUtf8("呼吸状态整体良好");
    else if (score >= 60) summaryText = QString::fromUtf8("呼吸状态基本正常，有少量异常");
    else if (score >= 40) summaryText = QString::fromUtf8("呼吸状态需关注，异常比例较高");
    else summaryText = QString::fromUtf8("呼吸状态较差，建议及时检查");
    m_scoreSummary->setText(summaryText);

    // Score color
    QString scoreColor = score >= 80 ? "#4CAF50" : score >= 60 ? "#FFB74D" : score >= 40 ? "#FF9800" : "#FF5252";
    m_scoreValue->setStyleSheet(QString("font-size:42px;font-weight:900;color:%1;").arg(scoreColor));

    // Trend chart
    m_trendChart->clear();
    m_trendChart->addSeries("BPM", QColor(0x22c55e));

    // Compute local breath rate from resistance data (simplified zero-crossing)
    const QList<SensorSample> &samples = data.samples;
    QList<double> resVals;
    for (const auto &samp : samples) {
        if (samp.resistanceOhm > 0.0) resVals.append(samp.resistanceOhm);
    }
    // Sliding window 30s = 150 samples, step 6s = 30 samples
    const int windowSize = 150;
    const int step = 30;
    QList<double> bpmValues;
    for (int start = 0; start + windowSize <= resVals.size(); start += step) {
        // Detrend
        double sum = 0;
        for (int i = start; i < start + windowSize; ++i) sum += resVals[i];
        double mean = sum / windowSize;

        int crossings = 0;
        for (int i = start + 1; i < start + windowSize; ++i) {
            double prev = resVals[i - 1] - mean;
            double curr = resVals[i] - mean;
            if ((prev <= 0 && curr > 0) || (prev >= 0 && curr < 0)) crossings++;
        }
        double durationSec = windowSize / 5.0;  // 5Hz
        double bpm = (crossings / 2.0) / (durationSec / 60.0);
        if (bpm > 0 && bpm < 60) {
            m_trendChart->append(0, bpm);
            bpmValues.append(bpm);
        }
    }

    // Status distribution
    QMap<QString, double> pctMap;
    pctMap[QString::fromUtf8("呼吸正常")] = s.normalPct;
    pctMap[QString::fromUtf8("呼吸异常")] = s.abnormalPct;
    pctMap[QString::fromUtf8("呼吸过快")] = s.tachypneaPct;
    pctMap[QString::fromUtf8("呼吸过慢")] = s.bradypneaPct;
    pctMap[QString::fromUtf8("屏气")] = s.apneaPct;
    m_statusDistLabel->setText(buildStatusDistHtml(pctMap));

    // === Generate local report (always available) ===
    QString localReport = generateLocalReport(score, s, bpmValues, samples);
    displayReport(localReport);

    // === Try API enhancement (optional, background) ===
    requestApiEnhancement(score, s, bpmValues, samples);
}

// ────────────────────────────────────────────────────────
// Local report generation (no network required)
// ────────────────────────────────────────────────────────

QString HealthReportDialog::generateLocalReport(int score, const BreathSummary &s,
    const QList<double> &bpmValues, const QList<SensorSample> &samples)
{
    // Compute BPM statistics
    double bpmAvg = 0, bpmMin = 0, bpmMax = 0, bpmStd = 0;
    if (!bpmValues.isEmpty()) {
        double sum = 0;
        bpmMin = bpmValues.first();
        bpmMax = bpmValues.first();
        for (double v : bpmValues) {
            sum += v;
            if (v < bpmMin) bpmMin = v;
            if (v > bpmMax) bpmMax = v;
        }
        bpmAvg = sum / bpmValues.size();
        double varSum = 0;
        for (double v : bpmValues) varSum += (v - bpmAvg) * (v - bpmAvg);
        bpmStd = std::sqrt(varSum / bpmValues.size());
    }

    // Determine overall health level
    QString healthLevel;
    if (score >= 80) healthLevel = QString::fromUtf8("良好");
    else if (score >= 60) healthLevel = QString::fromUtf8("基本正常");
    else if (score >= 40) healthLevel = QString::fromUtf8("需关注");
    else healthLevel = QString::fromUtf8("需重视");

    // Build report in Markdown
    QString report;

    // Section 1: Overall assessment
    report += QString::fromUtf8("## 1. 总体评估\n\n");
    report += QString::fromUtf8("综合评分 **%1 / 100**，呼吸状态等级：**%2**。\n\n")
        .arg(score).arg(healthLevel);

    if (score >= 80) {
        report += QString::fromUtf8("呼吸模式整体平稳，各指标在正常范围内。继续保持当前状态即可。\n\n");
    } else if (score >= 60) {
        report += QString::fromUtf8("呼吸基本正常，但检测到少量异常波动。建议关注异常发生的时间段和诱因。\n\n");
    } else if (score >= 40) {
        report += QString::fromUtf8("呼吸状态存在明显异常，异常比例较高。建议结合自身情况排查可能的诱因（如压力、睡眠质量等）。\n\n");
    } else {
        report += QString::fromUtf8("呼吸状态较差，频繁出现异常。强烈建议关注自身健康状况，必要时咨询专业人士。\n\n");
    }

    // Section 2: Breathing rate analysis
    report += QString::fromUtf8("## 2. 呼吸频率分析\n\n");
    if (!bpmValues.isEmpty()) {
        report += QString::fromUtf8("| 指标 | 数值 |\n|------|------|\n");
        report += QString::fromUtf8("| 平均频率 | %1 次/分 |\n").arg(bpmAvg, 0, 'f', 1);
        report += QString::fromUtf8("| 最低频率 | %1 次/分 |\n").arg(bpmMin, 0, 'f', 1);
        report += QString::fromUtf8("| 最高频率 | %1 次/分 |\n").arg(bpmMax, 0, 'f', 1);
        report += QString::fromUtf8("| 波动标准差 | %1 次/分 |\n\n").arg(bpmStd, 0, 'f', 1);

        if (bpmAvg >= 12 && bpmAvg <= 20) {
            report += QString::fromUtf8("平均呼吸频率在成人正常范围（12-20次/分）内。\n\n");
        } else if (bpmAvg > 20) {
            report += QString::fromUtf8("平均呼吸频率偏高（>20次/分），可能存在呼吸过快的情况。\n\n");
        } else if (bpmAvg > 0) {
            report += QString::fromUtf8("平均呼吸频率偏低（<12次/分），可能存在呼吸过慢的情况。\n\n");
        }

        if (bpmStd > 4.0) {
            report += QString::fromUtf8("呼吸频率波动较大（标准差 %1），提示呼吸节律不够稳定。\n\n").arg(bpmStd, 0, 'f', 1);
        }
    } else {
        report += QString::fromUtf8("数据不足，无法计算呼吸频率。需要至少30秒连续数据。\n\n");
    }

    // Section 3: Breathing pattern analysis
    report += QString::fromUtf8("## 3. 呼吸模式分析\n\n");
    report += QString::fromUtf8("| 状态 | 占比 |\n|------|------|\n");
    report += QString::fromUtf8("| 呼吸正常 | %1% |\n").arg(s.normalPct, 0, 'f', 1);
    report += QString::fromUtf8("| 呼吸异常 | %1% |\n").arg(s.abnormalPct, 0, 'f', 1);
    report += QString::fromUtf8("| 呼吸过快 | %1% |\n").arg(s.tachypneaPct, 0, 'f', 1);
    report += QString::fromUtf8("| 呼吸过慢 | %1% |\n").arg(s.bradypneaPct, 0, 'f', 1);
    report += QString::fromUtf8("| 屏气 | %1% |\n\n").arg(s.apneaPct, 0, 'f', 1);

    if (s.normalPct >= 80) {
        report += QString::fromUtf8("呼吸模式稳定，正常呼吸占比超过80%。\n\n");
    } else if (s.normalPct >= 60) {
        report += QString::fromUtf8("呼吸模式基本稳定，但存在较多非正常呼吸时段。\n\n");
    } else {
        report += QString::fromUtf8("非正常呼吸占比较高，需要关注。\n\n");
    }

    // Section 4: Anomaly events
    report += QString::fromUtf8("## 4. 异常事件记录\n\n");
    report += QString::fromUtf8("- 异常分数均值：%1\n").arg(s.anomalyScoreMean, 0, 'f', 2);
    report += QString::fromUtf8("- 异常分数峰值：%1\n").arg(s.anomalyScoreMax, 0, 'f', 2);
    report += QString::fromUtf8("- 异常分数标准差：%1\n").arg(s.anomalyScoreStd, 0, 'f', 2);

    if (s.alertCount > 0) {
        report += QString::fromUtf8("- 按压报警次数：%1 次\n").arg(s.alertCount);
        report += QString::fromUtf8("- 报警累计持续：%1 秒\n").arg(s.alertDurationMs / 1000.0, 0, 'f', 1);
        report += QString::fromUtf8("- 峰值力：%1 N\n").arg(s.peakForceN, 0, 'f', 1);
    } else {
        report += QString::fromUtf8("- 无按压报警事件\n");
    }
    report += "\n";

    // Section 5: Trend observation
    report += QString::fromUtf8("## 5. 趋势观察\n\n");
    report += QString::fromUtf8("- 采集时长：%1 秒\n").arg(s.durationSec, 0, 'f', 0);
    report += QString::fromUtf8("- 有效样本数：%1\n").arg(s.totalSamples);

    if (bpmValues.size() >= 3) {
        double firstHalf = 0, secondHalf = 0;
        int half = bpmValues.size() / 2;
        for (int i = 0; i < half; ++i) firstHalf += bpmValues[i];
        for (int i = half; i < bpmValues.size(); ++i) secondHalf += bpmValues[i];
        firstHalf /= half;
        secondHalf /= (bpmValues.size() - half);

        double trend = secondHalf - firstHalf;
        if (qAbs(trend) < 1.0) {
            report += QString::fromUtf8("- 呼吸频率趋势：**平稳**，前后半段无显著变化\n");
        } else if (trend > 0) {
            report += QString::fromUtf8("- 呼吸频率趋势：**上升**（+ %1 次/分），后半段呼吸加快\n")
                .arg(trend, 0, 'f', 1);
        } else {
            report += QString::fromUtf8("- 呼吸频率趋势：**下降**（%1 次/分），后半段呼吸减缓\n")
                .arg(trend, 0, 'f', 1);
        }
    }
    report += "\n";

    // Section 6: Health suggestions
    report += QString::fromUtf8("## 6. 健康建议\n\n");

    if (score >= 80) {
        report += QString::fromUtf8(
            "1. 保持当前良好的呼吸习惯\n"
            "2. 建议定期监测，形成呼吸健康档案\n"
            "3. 适当进行深呼吸或冥想练习，有助于维持呼吸稳定\n\n");
    } else if (score >= 60) {
        report += QString::fromUtf8(
            "1. 关注异常发生的时段和环境因素\n"
            "2. 保持规律作息，避免过度疲劳\n"
            "3. 尝试腹式呼吸练习（每次5-10分钟）\n"
            "4. 如异常持续出现，建议记录并与专业人士沟通\n\n");
    } else if (score >= 40) {
        report += QString::fromUtf8(
            "1. 建议增加监测频率，记录每天的呼吸状态变化\n"
            "2. 避免在空气不流通的环境中长时间停留\n"
            "3. 学习并练习腹式呼吸和4-7-8呼吸法\n"
            "4. 保持规律运动，但避免高强度运动后立即监测\n"
            "5. 如伴有胸闷、气短等症状，建议及时就医\n\n");
    } else {
        report += QString::fromUtf8(
            "1. 强烈建议尽快就医，进行专业呼吸功能检查\n"
            "2. 持续监测并记录数据，为医疗诊断提供参考\n"
            "3. 避免剧烈运动和情绪波动\n"
            "4. 保持环境空气流通，避免烟尘刺激\n"
            "5. 如出现严重呼吸困难，请立即拨打急救电话\n\n");
    }

    report += QString::fromUtf8("---\n*本报告由本地算法自动生成，仅供参考，不构成医学诊断。*\n");

    return report;
}

// ────────────────────────────────────────────────────────
// API enhancement (optional, called after local report)
// ────────────────────────────────────────────────────────

void HealthReportDialog::requestApiEnhancement(int score, const BreathSummary &s,
    const QList<double> &bpmValues, const QList<SensorSample> &samples)
{
    QSettings settings("Zhongyan", "ResistanceForceMonitor");
    QString apiKey = settings.value("api/key", "").toString();
    if (apiKey.isEmpty()) {
        // No API key configured — skip silently, local report is already displayed
        return;
    }

    // Build prompt
    QString systemPrompt = QString::fromUtf8(
        "你是一个专业的呼吸健康分析助手。请根据用户提供的呼吸传感器数据，"
        "生成一份通俗易懂的呼吸健康分析报告。\n\n"
        "报告格式要求（中文 Markdown）：\n"
        "1. 【总体评估】—— 用 2-3 句话概括呼吸状态\n"
        "2. 【呼吸频率分析】—— 平均频率、波动范围、是否在正常区间（12-20次/分为成人正常）\n"
        "3. 【呼吸模式分析】—— 各状态占比，重点标注异常时段\n"
        "4. 【异常事件记录】—— 列出检测到的异常事件\n"
        "5. 【趋势观察】—— 数据变化趋势\n"
        "6. 【健康建议】—— 基于数据给出 3-5 条具体建议\n\n"
        "注意事项：\n"
        "- 用通俗易懂的中文，避免过多医学术语\n"
        "- 如果数据不足，如实说明\n"
        "- 不要给出医学诊断，仅提供参考建议\n"
        "- 使用 Markdown 格式");

    QString userPrompt = QString::fromUtf8(
        "请分析以下呼吸监测数据并生成健康报告：\n\n"
        "【数据概要】\n"
        "- 采集时长：%1 秒\n"
        "- 采样频率：5Hz\n"
        "- 总样本数：%2\n"
        "- 本地评分：%3/100\n\n"
        "【呼吸状态统计】\n"
        "- 呼吸正常：%4%\n"
        "- 呼吸异常：%5%\n"
        "- 呼吸过快：%6%\n"
        "- 呼吸过慢：%7%\n"
        "- 屏气：%8%\n\n"
        "【异常分数统计】\n"
        "- 平均值：%9\n"
        "- 最大值：%10\n"
        "- 标准差：%11\n\n")
        .arg(s.durationSec, 0, 'f', 1)
        .arg(s.totalSamples)
        .arg(score)
        .arg(s.normalPct, 0, 'f', 1)
        .arg(s.abnormalPct, 0, 'f', 1)
        .arg(s.tachypneaPct, 0, 'f', 1)
        .arg(s.bradypneaPct, 0, 'f', 1)
        .arg(s.apneaPct, 0, 'f', 1)
        .arg(s.anomalyScoreMean, 0, 'f', 2)
        .arg(s.anomalyScoreMax, 0, 'f', 2)
        .arg(s.anomalyScoreStd, 0, 'f', 2);

    if (s.alertCount > 0) {
        userPrompt += QString::fromUtf8("【报警事件】\n- 共 %1 次按压报警\n- 累计持续 %2 秒\n- 峰值力 %3 N\n")
            .arg(s.alertCount)
            .arg(s.alertDurationMs / 1000.0, 0, 'f', 1)
            .arg(s.peakForceN, 0, 'f', 1);
    }

    // Show loading hint in the report
    m_loadingBar->setVisible(true);
    m_loadingLabel->setVisible(true);
    m_loadingLabel->setText(QString::fromUtf8("正在请求 AI 增强分析..."));
    m_loadingLabel->setStyleSheet("color:#A0A0A0;font-size:12px;");

    requestReport(userPrompt, systemPrompt);
}

// ────────────────────────────────────────────────────────
// API request (fixed auth header format)
// ────────────────────────────────────────────────────────

void HealthReportDialog::requestReport(const QString &userPrompt, const QString &systemPrompt)
{
    QSettings settings("Zhongyan", "ResistanceForceMonitor");
    QString apiKey = settings.value("api/key").toString();
    QString endpoint = settings.value("api/endpoint",
        "https://api.xiaomimimo.com/v1/chat/completions").toString();

    QJsonObject body;
    body["model"] = "mimo-v2.5";
    body["max_completion_tokens"] = 4096;

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    body["messages"] = messages;

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Support both OpenAI-style Bearer auth and custom api-key header
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("requestStartTime", QDateTime::currentMSecsSinceEpoch());

    // 60-second timeout (increased from 30s)
    QTimer::singleShot(60000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
}

void HealthReportDialog::onApiReply(QNetworkReply *reply)
{
    m_loadingBar->setVisible(false);
    m_loadingLabel->setVisible(false);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // API failed — local report is already displayed, just note the failure
        m_loadingLabel->setVisible(true);
        m_loadingLabel->setText(QString::fromUtf8("AI 增强不可用（网络或API错误）"));
        m_loadingLabel->setStyleSheet("color:#FFB74D;font-size:12px;");
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        m_loadingLabel->setVisible(true);
        m_loadingLabel->setText(QString::fromUtf8("AI 增强：API 返回格式异常"));
        m_loadingLabel->setStyleSheet("color:#FFB74D;font-size:12px;");
        return;
    }

    QJsonObject obj = doc.object();

    // Check for API error
    if (obj.contains("error")) {
        QJsonObject err = obj["error"].toObject();
        m_loadingLabel->setVisible(true);
        m_loadingLabel->setText(QString::fromUtf8("AI 增强：API 错误 - %1").arg(err["message"].toString()));
        m_loadingLabel->setStyleSheet("color:#FFB74D;font-size:12px;");
        return;
    }

    // Extract content text (OpenAI-compatible format)
    const QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        return;
    }

    QString text;
    for (const QJsonValue &v : choices) {
        QJsonObject choice = v.toObject();
        QJsonObject message = choice["message"].toObject();
        if (message.contains("content")) {
            text += message["content"].toString();
        }
    }

    if (text.isEmpty()) {
        return;
    }

    // Replace local report with AI-enhanced version
    displayReport(text);
    m_loadingLabel->setVisible(true);
    m_loadingLabel->setText(QString::fromUtf8("AI 增强分析已完成"));
    m_loadingLabel->setStyleSheet("color:#4CAF50;font-size:12px;");
}

void HealthReportDialog::displayReport(const QString &markdown)
{
    // Convert markdown to HTML (simple conversion for Qt compatibility)
    QTextDocument doc;
    doc.setMarkdown(markdown);
    m_aiBrowser->setHtml(doc.toHtml());
    m_exportHtmlBtn->setEnabled(true);
}

// ────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────

void HealthReportDialog::showError(const QString &msg)
{
    m_aiBrowser->setHtml(QString("<p style='color:#FF5252;padding:20px;'>%1</p>").arg(msg));
    m_loadingLabel->setText(QString::fromUtf8("生成失败"));
    m_loadingLabel->setStyleSheet("color:#FF5252;font-size:12px;");
}

void HealthReportDialog::setLoading(bool loading)
{
    m_loadingBar->setVisible(loading);
    m_loadingLabel->setVisible(loading);
    m_generateBtn->setEnabled(!loading);
    if (loading) {
        m_loadingLabel->setText(QString::fromUtf8("正在生成报告..."));
        m_loadingLabel->setStyleSheet("color:#A0A0A0;font-size:12px;");
    }
}

int HealthReportDialog::calculateLocalScore(double normalPct, double anomalyAvg)
{
    // Score = normalPct contribution (0-70) + anomalyAvg penalty (0-30)
    int normalScore = static_cast<int>(normalPct * 0.7);
    int anomalyPenalty = qBound(0, static_cast<int>(anomalyAvg * 10), 30);
    return qBound(0, normalScore + (30 - anomalyPenalty), 100);
}

QString HealthReportDialog::buildStatusDistHtml(const QMap<QString, double> &pctMap)
{
    struct ColorEntry { QString label; QString color; };
    const QList<ColorEntry> entries = {
        { QString::fromUtf8("呼吸正常"), "#4CAF50" },
        { QString::fromUtf8("呼吸异常"), "#FF5252" },
        { QString::fromUtf8("呼吸过快"), "#FF9800" },
        { QString::fromUtf8("呼吸过慢"), "#42A5F5" },
        { QString::fromUtf8("屏气"),     "#AB47BC" }
    };

    QString html;
    for (const auto &e : entries) {
        double pct = pctMap.value(e.label, 0.0);
        int barWidth = qBound(0, static_cast<int>(pct * 2), 200);
        html += QString("<div style='margin:6px 0;'>"
                        "<span style='color:#A0A0A0;font-size:12px;display:inline-block;width:60px;'>%1</span>"
                        "<span style='display:inline-block;background:%2;height:14px;"
                        "border-radius:3px;width:%3px;'></span>"
                        "<span style='color:#E0E0E0;font-size:12px;margin-left:8px;'>%4%</span>"
                        "</div>")
            .arg(e.label, e.color).arg(barWidth).arg(pct, 0, 'f', 1);
    }
    return html;
}

void HealthReportDialog::exportHtml()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出报告"),
        QString("breath_report_%1.html")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "HTML (*.html)");
    if (filePath.isEmpty()) return;

    // Build standalone HTML
    QString html = QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>呼吸健康报告</title>"
        "<style>"
        "body{font-family:'Microsoft YaHei',sans-serif;background:#10131a;color:#E0E0E0;"
        "max-width:800px;margin:0 auto;padding:30px;}"
        "h1{color:#F0F0F0;font-size:24px;}"
        "h2{color:#0078D4;font-size:18px;margin-top:24px;}"
        "h3{color:#A0A0A0;font-size:15px;}"
        ".score{font-size:48px;font-weight:900;color:#0078D4;text-align:center;}"
        ".summary{color:#A0A0A0;text-align:center;font-size:16px;}"
        "table{border-collapse:collapse;width:100%%;margin:12px 0;}"
        "td,th{border:1px solid #3D3D3D;padding:8px 12px;text-align:left;}"
        "th{background:#171c26;color:#A0A0A0;}"
        "pre{background:#1e293b;padding:12px;border-radius:4px;overflow-x:auto;}"
        "code{color:#22d3ee;}"
        "ul,ol{line-height:1.8;}"
        "</style></head><body>"
        "<h1>呼吸健康报告</h1>"
        "<p style='color:#A0A0A0;'>生成时间：%1</p>"
        "<div class='score'>%2 / 100</div>"
        "<div class='summary'>%3</div>"
        "<hr style='border-color:#3D3D3D;margin:24px 0;'>"
        "%4"
        "</body></html>")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"),
             m_scoreValue->text().split(" /").first().trimmed(),
             m_scoreSummary->text(),
             m_aiBrowser->toHtml());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QString::fromUtf8("导出失败"),
                              QString::fromUtf8("无法写入文件: %1").arg(filePath));
        return;
    }
    file.write(html.toUtf8());
    file.close();

    QMessageBox::information(this, QString::fromUtf8("导出成功"),
                             QString::fromUtf8("报告已导出到:\n%1").arg(filePath));
}
