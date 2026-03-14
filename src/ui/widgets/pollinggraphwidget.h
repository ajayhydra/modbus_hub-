#pragma once

#include <QtGlobal>  // For QT_VERSION macros

// QtCharts is only available in Qt6, so make this widget optional
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define HAVE_QTCHARTS 1

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QGroupBox>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLegend>

// Data point for a single reading
struct PollDataPoint {
    qint64 timestampMs;   // milliseconds since epoch
    qreal  value;
};

// Per-device series info
struct DeviceSeriesInfo {
    int deviceId;
    QString deviceName;
    QLineSeries* series;
    QVector<PollDataPoint> history;
    QCheckBox* visibilityCheckbox;
    QColor color;
    qreal minValue;
    qreal maxValue;
    qreal lastValue;
    int totalPoints;
};

// This widget displays polling activity for ALL monitored devices as a
// multi-series line graph.  The parent feeds data via addDataPoint() from
// the existing onMonitorAllTimer() slot – no extra Modbus I/O is performed.
class PollingGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PollingGraphWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_chart(new QChart)
        , m_axisX(new QDateTimeAxis)
        , m_axisY(new QValueAxis)
        , m_paused(false)
        , m_windowSeconds(120)
        , m_globalMinY(0)
        , m_globalMaxY(100)
        , m_firstDataReceived(false)
    {
        // --- Chart setup ---
        m_chart->setTitle("Device Polling Graph");
        m_chart->setAnimationOptions(QChart::NoAnimation);  // real-time, no anim
        m_chart->legend()->setVisible(true);
        m_chart->legend()->setAlignment(Qt::AlignBottom);

        // X-axis: date-time
        m_axisX->setTitleText("Time");
        m_axisX->setFormat("hh:mm:ss");
        m_axisX->setTickCount(7);
        m_chart->addAxis(m_axisX, Qt::AlignBottom);

        // Y-axis: register values
        m_axisY->setTitleText("Register Value");
        m_axisY->setLabelFormat("%d");
        m_axisY->setRange(0, 100);
        m_chart->addAxis(m_axisY, Qt::AlignLeft);

        // Chart view
        m_chartView = new QChartView(m_chart);
        m_chartView->setRenderHint(QPainter::Antialiasing);
        m_chartView->setMinimumHeight(280);

        // --- Controls toolbar ---
        QHBoxLayout* toolbar = new QHBoxLayout;

        m_btnPause = new QPushButton("Pause");
        m_btnPause->setCheckable(true);
        connect(m_btnPause, &QPushButton::toggled, this, [this](bool checked) {
            m_paused = checked;
            m_btnPause->setText(checked ? "Resume" : "Pause");
        });
        toolbar->addWidget(m_btnPause);

        QPushButton* btnClear = new QPushButton("Clear Graph");
        connect(btnClear, &QPushButton::clicked, this, &PollingGraphWidget::clearAll);
        toolbar->addWidget(btnClear);

        // Time window selector
        toolbar->addWidget(new QLabel("Window:"));
        QPushButton* btn60 = new QPushButton("1 min");
        QPushButton* btn120 = new QPushButton("2 min");
        QPushButton* btn300 = new QPushButton("5 min");
        QPushButton* btn600 = new QPushButton("10 min");
        connect(btn60,  &QPushButton::clicked, this, [this]{ setTimeWindow(60);  });
        connect(btn120, &QPushButton::clicked, this, [this]{ setTimeWindow(120); });
        connect(btn300, &QPushButton::clicked, this, [this]{ setTimeWindow(300); });
        connect(btn600, &QPushButton::clicked, this, [this]{ setTimeWindow(600); });
        toolbar->addWidget(btn60);
        toolbar->addWidget(btn120);
        toolbar->addWidget(btn300);
        toolbar->addWidget(btn600);

        toolbar->addStretch();

        // --- Device visibility panel (scrollable checkboxes) ---
        m_devicePanel = new QWidget;
        m_devicePanelLayout = new QHBoxLayout(m_devicePanel);
        m_devicePanelLayout->setContentsMargins(0, 0, 0, 0);
        m_devicePanelLayout->addWidget(new QLabel("Show:"));
        m_devicePanelLayout->addStretch();

        QScrollArea* scroll = new QScrollArea;
        scroll->setWidget(m_devicePanel);
        scroll->setWidgetResizable(true);
        scroll->setMaximumHeight(50);
        scroll->setFrameShape(QFrame::NoFrame);

        // --- Statistics label ---
        m_statsLabel = new QLabel("No data yet");
        m_statsLabel->setWordWrap(true);

        // --- Main layout ---
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->addLayout(toolbar);
        mainLayout->addWidget(m_chartView, 1);
        mainLayout->addWidget(scroll);
        mainLayout->addWidget(m_statsLabel);
        setLayout(mainLayout);

        // Refresh display periodically (rolling window)
        m_refreshTimer = new QTimer(this);
        connect(m_refreshTimer, &QTimer::timeout, this, &PollingGraphWidget::refreshAxes);
        m_refreshTimer->start(1000);
    }

    // ----------------------------------------------------------------
    // Public API – called from MainWindow::onMonitorAllTimer()
    // ----------------------------------------------------------------

    // Add a data point for a device.  Creates the series automatically
    // the first time a device ID is seen.
    void addDataPoint(int deviceId, const QString& deviceName, qreal value)
    {
        if (m_paused) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // Create series on first sight of this device
        if (!m_devices.contains(deviceId)) {
            createDeviceSeries(deviceId, deviceName);
        }

        DeviceSeriesInfo& info = m_devices[deviceId];
        info.series->append(now, value);
        info.history.append({now, value});
        info.lastValue = value;
        info.totalPoints++;
        if (value < info.minValue) info.minValue = value;
        if (value > info.maxValue) info.maxValue = value;

        // Update device name if it changed
        if (info.deviceName != deviceName) {
            info.deviceName = deviceName;
            info.series->setName(deviceName);
            info.visibilityCheckbox->setText(deviceName);
        }

        // Trim old points beyond 2× window to save memory
        qint64 cutoff = now - (m_windowSeconds * 2 * 1000);
        while (!info.history.isEmpty() && info.history.first().timestampMs < cutoff) {
            info.history.removeFirst();
        }

        // Update global Y range
        if (!m_firstDataReceived) {
            m_globalMinY = value;
            m_globalMaxY = value;
            m_firstDataReceived = true;
        } else {
            if (value < m_globalMinY) m_globalMinY = value;
            if (value > m_globalMaxY) m_globalMaxY = value;
        }

        refreshAxes();
        updateStats();
    }

    // Check if graph is tracking any devices
    bool hasDevices() const { return !m_devices.isEmpty(); }

public slots:
    void clearAll()
    {
        for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
            it.value().series->clear();
            it.value().history.clear();
            it.value().totalPoints = 0;
            it.value().minValue = 0;
            it.value().maxValue = 0;
            it.value().lastValue = 0;
        }
        m_globalMinY = 0;
        m_globalMaxY = 100;
        m_firstDataReceived = false;
        m_statsLabel->setText("Graph cleared");
    }

    void setTimeWindow(int seconds)
    {
        m_windowSeconds = seconds;
        refreshAxes();
    }

private:
    // Predefined color palette for devices
    static QColor colorForIndex(int idx) {
        static const QColor palette[] = {
            QColor(0,   122, 255),   // Blue
            QColor(255,  59,  48),   // Red
            QColor( 52, 199,  89),   // Green
            QColor(255, 149,   0),   // Orange
            QColor(175,  82, 222),   // Purple
            QColor(255,  45,  85),   // Pink
            QColor( 90, 200, 250),   // Teal
            QColor(255, 204,   0),   // Yellow
            QColor( 88,  86, 214),   // Indigo
            QColor(162, 132,  94),   // Brown
        };
        return palette[idx % 10];
    }

    void createDeviceSeries(int deviceId, const QString& name)
    {
        DeviceSeriesInfo info;
        info.deviceId = deviceId;
        info.deviceName = name;
        info.series = new QLineSeries;
        info.series->setName(name);
        int colorIdx = m_devices.size();
        info.color = colorForIndex(colorIdx);
        QPen pen(info.color);
        pen.setWidth(2);
        info.series->setPen(pen);
        info.minValue = 0;
        info.maxValue = 0;
        info.lastValue = 0;
        info.totalPoints = 0;

        m_chart->addSeries(info.series);
        info.series->attachAxis(m_axisX);
        info.series->attachAxis(m_axisY);

        // Visibility checkbox
        info.visibilityCheckbox = new QCheckBox(name);
        info.visibilityCheckbox->setChecked(true);
        QPalette pal = info.visibilityCheckbox->palette();
        pal.setColor(QPalette::WindowText, info.color);
        info.visibilityCheckbox->setPalette(pal);
        info.visibilityCheckbox->setStyleSheet(
            QString("QCheckBox { color: %1; font-weight: bold; }").arg(info.color.name()));

        connect(info.visibilityCheckbox, &QCheckBox::toggled, this, [this, deviceId](bool visible) {
            if (m_devices.contains(deviceId)) {
                m_devices[deviceId].series->setVisible(visible);
            }
        });

        // Insert before the stretch item
        m_devicePanelLayout->insertWidget(m_devicePanelLayout->count() - 1, info.visibilityCheckbox);

        m_devices.insert(deviceId, info);
    }

    void refreshAxes()
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 windowStart = now - (m_windowSeconds * 1000);

        m_axisX->setRange(
            QDateTime::fromMSecsSinceEpoch(windowStart),
            QDateTime::fromMSecsSinceEpoch(now)
        );

        // Auto-scale Y with 10% margin
        if (m_firstDataReceived) {
            qreal range = m_globalMaxY - m_globalMinY;
            if (range < 1.0) range = 1.0;
            qreal margin = range * 0.10;
            qreal yMin = m_globalMinY - margin;
            qreal yMax = m_globalMaxY + margin;
            if (yMin < 0) yMin = 0;
            m_axisY->setRange(yMin, yMax);
        }
    }

    void updateStats()
    {
        QString stats;
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            const DeviceSeriesInfo& d = it.value();
            if (!stats.isEmpty()) stats += "  |  ";
            stats += QString("<span style='color:%1;font-weight:bold;'>%2</span>: "
                             "Last=%3  Min=%4  Max=%5  Points=%6")
                .arg(d.color.name())
                .arg(d.deviceName)
                .arg(d.lastValue, 0, 'f', 0)
                .arg(d.minValue, 0, 'f', 0)
                .arg(d.maxValue, 0, 'f', 0)
                .arg(d.totalPoints);
        }
        m_statsLabel->setText(stats);
    }

    // Members
    QChart*         m_chart;
    QChartView*     m_chartView;
    QDateTimeAxis*  m_axisX;
    QValueAxis*     m_axisY;

    QMap<int, DeviceSeriesInfo> m_devices;  // keyed by device ID

    QPushButton*    m_btnPause;
    QLabel*         m_statsLabel;
    QWidget*        m_devicePanel;
    QHBoxLayout*    m_devicePanelLayout;
    QTimer*         m_refreshTimer;

    bool   m_paused;
    int    m_windowSeconds;
    qreal  m_globalMinY;
    qreal  m_globalMaxY;
    bool   m_firstDataReceived;
};

#endif // QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
