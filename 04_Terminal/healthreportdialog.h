#ifndef HEALTHREPORTDIALOG_H
#define HEALTHREPORTDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "widget.h"

class QLabel;
class QTextBrowser;
class QPushButton;
class QProgressBar;
class RealtimeChart;
class Widget;

class HealthReportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HealthReportDialog(Widget *mainWidget, QWidget *parent = nullptr);

private slots:
    void generateReport();
    void onApiReply(QNetworkReply *reply);

private:
    void setupUi();
    QString generateLocalReport(int score, const BreathSummary &s,
                                const QList<double> &bpmValues,
                                const QList<SensorSample> &samples);
    void requestApiEnhancement(int score, const BreathSummary &s,
                               const QList<double> &bpmValues,
                               const QList<SensorSample> &samples);
    void requestReport(const QString &userPrompt, const QString &systemPrompt);
    void displayReport(const QString &markdown);
    void showError(const QString &msg);
    void setLoading(bool loading);
    int calculateLocalScore(double normalPct, double anomalyAvg);
    QString buildStatusDistHtml(const QMap<QString, double> &pctMap);
    void exportHtml();

    Widget *m_mainWidget;
    QNetworkAccessManager *m_nam;

    QLabel *m_scoreValue;
    QLabel *m_scoreSummary;
    RealtimeChart *m_trendChart;
    QLabel *m_statusDistLabel;
    QTextBrowser *m_aiBrowser;
    QPushButton *m_generateBtn;
    QPushButton *m_exportHtmlBtn;
    QProgressBar *m_loadingBar;
    QLabel *m_loadingLabel;
};

#endif // HEALTHREPORTDIALOG_H
