#include "realtimechart.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

RealtimeChart::RealtimeChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void RealtimeChart::addSeries(const QString &name, const QColor &color)
{
    ChartSeries series;
    series.name = name;
    series.color = color;
    series.data.reserve(m_maxPoints + 16);
    m_series.append(series);
    update();
}

void RealtimeChart::append(int seriesIndex, double value)
{
    if (seriesIndex < 0 || seriesIndex >= m_series.size()) {
        return;
    }

    auto &data = m_series[seriesIndex].data;
    data.append(value);
    while (data.size() > m_maxPoints) {
        data.remove(0);
    }

    m_yDirty = true;
    update();
}

void RealtimeChart::clear()
{
    for (auto &series : m_series) {
        series.data.clear();
    }
    m_yDirty = true;
    update();
}

void RealtimeChart::setMaxPoints(int count)
{
    m_maxPoints = qMax(16, count);
    for (auto &series : m_series) {
        while (series.data.size() > m_maxPoints) {
            series.data.remove(0);
        }
    }
    m_yDirty = true;
    update();
}

void RealtimeChart::setYRange(double minValue, double maxValue)
{
    m_yMin = minValue;
    m_yMax = maxValue;
    m_autoScaleY = false;
    update();
}

void RealtimeChart::setAutoScaleY(bool enabled)
{
    m_autoScaleY = enabled;
    m_yDirty = true;
    update();
}

void RealtimeChart::setUnit(const QString &unit)
{
    m_unit = unit;
    update();
}

void RealtimeChart::recalculateYRange()
{
    double yMin = 0.0;
    double yMax = 0.0;
    bool found = false;

    for (const auto &series : m_series) {
        for (double value : series.data) {
            if (!found) {
                yMin = value;
                yMax = value;
                found = true;
            } else {
                yMin = qMin(yMin, value);
                yMax = qMax(yMax, value);
            }
        }
    }

    if (!found) {
        m_cachedYMin = -10.0;
        m_cachedYMax = 10.0;
    } else if (qFuzzyCompare(yMin, yMax)) {
        const double span = qMax(1.0, qAbs(yMin) * 0.2);
        m_cachedYMin = yMin - span;
        m_cachedYMax = yMax + span;
    } else {
        const double margin = qMax(0.5, (yMax - yMin) * 0.15);
        m_cachedYMin = yMin - margin;
        m_cachedYMax = yMax + margin;
    }

    m_yDirty = false;
}

void RealtimeChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int left = qBound(42, width() / 9, 64);
    const int right = qBound(8, width() / 40, 16);
    const int top = 26;
    const int bottom = 28;
    const QRect plotRect(left, top, width() - left - right, height() - top - bottom);

    painter.fillRect(rect(), QColor("#151820"));
    if (plotRect.width() <= 20 || plotRect.height() <= 20) {
        return;
    }

    painter.fillRect(plotRect, QColor("#0f1218"));

    if (m_autoScaleY && m_yDirty) {
        recalculateYRange();
    }

    const double yLow = m_autoScaleY ? m_cachedYMin : m_yMin;
    const double yHigh = m_autoScaleY ? m_cachedYMax : m_yMax;
    const double ySpan = qMax(0.000001, yHigh - yLow);

    painter.setFont(QFont("Consolas", 8));
    for (int i = 0; i <= 4; ++i) {
        const int y = plotRect.top() + plotRect.height() * i / 4;
        painter.setPen(QPen(QColor("#283142"), 1));
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);

        const double value = yHigh - ySpan * i / 4.0;
        painter.setPen(QColor("#8b98aa"));
        painter.drawText(0, y - 8, left - 8, 16,
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value, 'f', 2));
    }

    painter.setPen(QPen(QColor("#334155"), 1));
    painter.drawRect(plotRect);

    bool hasData = false;
    int maxSize = 0;
    for (const auto &series : m_series) {
        hasData = hasData || !series.data.isEmpty();
        maxSize = qMax(maxSize, series.data.size());
    }

    if (!hasData) {
        painter.setPen(QColor("#6b7280"));
        painter.setFont(QFont("Microsoft YaHei", 11));
        painter.drawText(plotRect, Qt::AlignCenter, QString::fromUtf8("等待数据"));
        return;
    }

    const double xStep = maxSize <= 1 ? plotRect.width()
                                      : static_cast<double>(plotRect.width()) / (m_maxPoints - 1);

    for (const auto &series : m_series) {
        if (series.data.isEmpty()) {
            continue;
        }

        QPainterPath path;
        for (int i = 0; i < series.data.size(); ++i) {
            const double value = series.data.at(i);
            const qreal x = plotRect.left() + i * xStep;
            qreal y = plotRect.bottom() - (value - yLow) / ySpan * plotRect.height();
            y = qBound(static_cast<qreal>(plotRect.top()), y, static_cast<qreal>(plotRect.bottom()));
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }

        QPainterPath fillPath = path;
        fillPath.lineTo(plotRect.left() + (series.data.size() - 1) * xStep, plotRect.bottom());
        fillPath.lineTo(plotRect.left(), plotRect.bottom());
        fillPath.closeSubpath();

        QColor fill = series.color;
        fill.setAlpha(28);
        QLinearGradient gradient(0, plotRect.top(), 0, plotRect.bottom());
        gradient.setColorAt(0, fill);
        gradient.setColorAt(1, QColor(0, 0, 0, 0));
        painter.fillPath(fillPath, gradient);

        painter.setPen(QPen(series.color, 2.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    int legendX = plotRect.left() + 8;
    const int legendY = plotRect.top() + 5;
    painter.setFont(QFont("Consolas", 9));
    for (const auto &series : m_series) {
        painter.setPen(series.color);
        painter.drawText(legendX, legendY, 180, 18,
                         Qt::AlignLeft | Qt::AlignVCenter, series.name);
        legendX += painter.fontMetrics().horizontalAdvance(series.name) + 24;
    }

    if (!m_unit.isEmpty()) {
        painter.setPen(QColor("#64748b"));
        painter.drawText(plotRect.right() - 80, 4, 80, 18,
                         Qt::AlignRight | Qt::AlignVCenter, m_unit);
    }
}
