#ifndef REALTIMECHART_H
#define REALTIMECHART_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

struct ChartSeries {
    QString name;
    QColor color;
    QVector<double> data;
};

class RealtimeChart : public QWidget
{
    Q_OBJECT

public:
    explicit RealtimeChart(QWidget *parent = nullptr);

    void addSeries(const QString &name, const QColor &color);
    void append(int seriesIndex, double value);
    void clear();
    void setMaxPoints(int count);
    void setYRange(double minValue, double maxValue);
    void setAutoScaleY(bool enabled);
    void setUnit(const QString &unit);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void recalculateYRange();

    QVector<ChartSeries> m_series;
    int m_maxPoints = 120;
    double m_yMin = -10.0;
    double m_yMax = 10.0;
    double m_cachedYMin = -10.0;
    double m_cachedYMax = 10.0;
    bool m_autoScaleY = true;
    bool m_yDirty = true;
    QString m_unit;
};

#endif // REALTIMECHART_H
