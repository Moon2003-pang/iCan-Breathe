#ifndef WIDGET_H
#define WIDGET_H

#include <QByteArray>
#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

#include <QJsonArray>
#include <QJsonObject>

class QFrame;
class QGridLayout;
class QProgressBar;
class QSlider;
class RealtimeChart;

struct SensorSample {
    qint64 timestampMs = 0;
    double adc = 0.0;
    double voltageMv = 0.0;
    double resistanceOhm = 0.0;
    double forceN = 0.0;
    bool hasAdc = false;
    bool hasVoltage = false;
    bool hasResistance = false;
    bool hasForce = false;
    QString breathStatus;       // ESP32 edge-AI: "正常" / "异常"
    double anomalyScore = 0.0;  // reconstruction error ratio
    int alert = 0;              // 0=normal, 1=press alarm (v0.1)
};

struct BreathSummary {
    int totalSamples = 0;
    double durationSec = 0.0;
    double normalPct = 0.0;
    double abnormalPct = 0.0;
    double tachypneaPct = 0.0;
    double bradypneaPct = 0.0;
    double apneaPct = 0.0;
    double anomalyScoreMean = 0.0;
    double anomalyScoreMax = 0.0;
    double anomalyScoreStd = 0.0;
    int alertCount = 0;
    qint64 alertDurationMs = 0;
    double peakForceN = 0.0;
    double avgBreathRateBpm = 0.0;
};

struct BreathReportData {
    QList<SensorSample> samples;
    BreathSummary summary;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;

    // Health report data access (Task-R1/R3)
    BreathReportData loadRecentBreathData(int minutes);
    BreathSummary loadHistoricalSummary(int minutes);

private slots:
    void startServer();
    void stopServer();
    void acceptClient();
    void clientDisconnected();
    void readClientData();
    void refreshUi();
    void clearData();
    void appendSimulatedSample();

    // Alert slots (Task-D)
    void sendClearAlert();
    void toggleAlertBlink();

    // Watchdog slot (Task-G)
    void onWatchdogTimeout();

    // Reminder slot (Task-I)
    void onReminderTimeout();

    // Health report slot (Task-R3)
    void openHealthReport();

private:
    void setupUi();
    void setupTopBar();
    void setupContent();
    void setupBottomBar();
    void setupMobileTopBar();
    void setupMobileContent();
    void setupMobileBottomBar();
    void applyStyles();

    QFrame *createPanel(const QString &title);
    QLabel *createMetricValue(const QString &text = "--");
    QLabel *createMetricCaption(const QString &text);
    QLineEdit *createNumberEdit(const QString &text, int width = 88);

    void parseIncomingLine(const QByteArray &line);
    bool parseJsonSample(const QByteArray &line, SensorSample *sample) const;
    void enrichSample(SensorSample *sample) const;
    void appendSample(const SensorSample &sample);
    void toggleCollect();
    void saveCollectedData();
    void triggerCalibrate();
    void analyzeBreath();
    void updateConnectionState();
    void updateMetrics();
    void updateCharts(const SensorSample &sample);
    void updateCalibrationFromUi();

    // Alert UI methods (Task-D)
    void showAlarm();
    void hideAlarm();

    // Emergency contacts (Task-F)
    void loadEmergencyContacts();
    void sendEmergencyMessage(const QString &alertInfo);

    // Threshold config (Task-E)
    void sendThresholdConfig();
    void updateThresholdButtonState();

    // Alert history (Task-H)
    void logAlertEvent(const QString &event, const QString &clearMethod = QString());
    void updateAlertPeakForce(double forceN);

    // Watchdog (Task-G)
    void resetWatchdog();
    void startWatchdog();
    void stopWatchdog();

    // Breath history CSV (Task-R1)
    void initBreathHistory();
    void flushCsvBuffer();
    void rotateCsvFile();

    int simulationIntervalMs() const;
    double resistanceFromVoltage(double voltageMv) const;
    double forceFromResistance(double resistanceOhm) const;
    QString formatResistance(double ohm) const;
    QString formatForce(double forceN) const;
    QString localIpHint() const;

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QByteArray m_rxBuffer;
    QTimer *m_uiTimer = nullptr;
    QTimer *m_simTimer = nullptr;
    QSettings *m_settings = nullptr;
    bool m_stoppingServer = false;

    SensorSample m_current;
    qint64 m_lastSampleWallMs = 0;
    quint64 m_sampleCount = 0;
    double m_lastRateHz = 0.0;

    double m_refOhm = 10000000.0;
    double m_vccMv = 3300.0;
    double m_forceScaleNPerMohm = 1.0;

    // Auto zero-force calibration (自监督学习)
    QList<double> m_baselineBuffer;
    qint64 m_baselineStartMs = 0;
    bool m_baselineReady = false;
    static constexpr int kBaselineDurationMs = 10000;
    double m_baselineR0 = 0.0;

    // Adaptive baseline: slow EMA tracks sensor drift
    bool m_adaptiveBaselineInit = false;
    double m_adaptiveR0 = 0.0;
    static constexpr double kAdaptiveAlpha = 0.002;
    static constexpr double kAdaptiveGuardRatio = 0.05;  // 5% of R0

    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_clientText = nullptr;
    QLabel *m_sampleCountText = nullptr;
    QLabel *m_rateText = nullptr;

    QLineEdit *m_portEdit = nullptr;
    QLineEdit *m_refEdit = nullptr;
    QLineEdit *m_vccEdit = nullptr;
    QLineEdit *m_zeroEdit = nullptr;
    QLineEdit *m_scaleEdit = nullptr;

    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_simButton = nullptr;
    QSlider *m_simSpeedSlider = nullptr;
    QLabel *m_simSpeedText = nullptr;

    QLabel *m_resistanceValue = nullptr;
    QLabel *m_forceValue = nullptr;
    QLabel *m_voltageValue = nullptr;
    QLabel *m_adcValue = nullptr;
    QLabel *m_timestampValue = nullptr;
    QLabel *m_dataSourceValue = nullptr;

    // Baseline calibration status
    QLabel *m_baselineStatus = nullptr;

    QProgressBar *m_forceBar = nullptr;
    RealtimeChart *m_forceChart = nullptr;
    RealtimeChart *m_resistanceChart = nullptr;

    // Data collection
    QLineEdit *m_forceInput = nullptr;
    QPushButton *m_collectButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_calibrateButton = nullptr;
    QLabel *m_collectStatus = nullptr;
    bool m_collecting = false;
    QList<QPair<SensorSample, double>> m_collectedData;

    // Breath analysis
    QCheckBox *m_breathModeCheck = nullptr;
    QLabel *m_breathStatusLabel = nullptr;
    QList<double> m_breathBuffer;
    static constexpr int kBreathWindowSamples = 150;

    // Display scaling: physical ohms → MOhms for UI
    double m_resistanceDisplayScale = 1000000.0;

    // Alert UI (Task-D)
    QLabel *m_alertLabel = nullptr;
    QPushButton *m_clearAlertButton = nullptr;
    QTimer *m_alertBlinkTimer = nullptr;
    bool m_alertActive = false;
    bool m_alertVisible = false;

    // Threshold config (Task-E)
    QLineEdit *m_thresholdForceEdit = nullptr;
    QLineEdit *m_thresholdHoldoffEdit = nullptr;
    QPushButton *m_sendThresholdButton = nullptr;

    // Emergency contacts (Task-F)
    QJsonArray m_emergencyContacts;
    bool m_emergencySent = false;

    // Connection watchdog (Task-G)
    QTimer *m_watchdogTimer = nullptr;
    bool m_watchdogAlerted = false;
    QLabel *m_watchdogStatus = nullptr;

    // Alert history (Task-H)
    qint64 m_alertTriggerTimeMs = 0;
    double m_alertPeakForceN = 0.0;

    // Second reminder (Task-I)
    QTimer *m_reminderTimer = nullptr;
    bool m_reminderActive = false;

    // Health report - data persistence (Task-R1)
    QFile *m_breathHistoryFile = nullptr;
    QTextStream *m_breathHistoryStream = nullptr;
    QList<SensorSample> m_csvWriteBuffer;
    static constexpr int kCsvFlushInterval = 50;  // flush every 50 samples (~10s)
    static constexpr qint64 kCsvMaxFileSize = 10 * 1024 * 1024;  // 10MB

    // Health report - UI (Task-R3)
    QPushButton *m_reportButton = nullptr;

    // Mobile overview health report block
    QLabel *m_healthReportSummary = nullptr;
    QPushButton *m_healthReportButton = nullptr;
    int m_healthReportRefreshCounter = 0;
    void updateHealthReportSummary();
};

#endif // WIDGET_H
