#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QString>
#include <QMainWindow>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>
#include <QTimer>
#include <QThread>

extern "C" {
    #include "modbus.h"
    #include "device_manager.h"
    #include "data_logger.h"
    #include "config.h"
    #include "app_logger.h"
}

// Forward declare packet_monitor functions (only init/clear used)
extern "C" {
    void packet_monitor_init(void);
    void packet_monitor_clear(void);
}

class GatewayThread;
class DeviceWorkerThread;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class PollingGraphWidget;
#endif

// Log categories for filtering
enum LogCategory {
    LOG_CAT_ALL = 0,
    LOG_CAT_MASTER,
    LOG_CAT_GATEWAY,
    LOG_CAT_DEVICES,
    LOG_CAT_SYSTEM
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    // Alarm event handler
    static void alarmEventCallback(const char* message, int device_id, int register_address, int value, int threshold);
    void handleAlarmEvent(const QString& message, int device_id, int register_address, int value, int threshold);

private slots:
    // Master tab slots
    void onModeChanged(int index);
    void onConnect();
    void onDisconnect();
    void onRead();
    void onWrite();
    void onClearData();
    void onPollToggled(bool checked);
    void onPollTimer();
    void onFunctionChanged(int index);
    void onFormatChanged(int index);
    
    // Gateway tab slots
    void onGatewayModeChanged(bool checked);
    void onStartGateway();
    void onStopGateway();
    
    // Device tab slots
    void onAddDevice();
    void onRemoveDevice();
    void onEditDevice();
    void onDeviceSelected(int row, int column);
    void onConnectAll();
    void onDisconnectAll();
    void onMonitorAll();
    void onStopMonitor();
    void onScanNetwork();
    void onStopScan();
    void onScanTimer();

    // Slots receiving results from DeviceWorkerThread (queued connection)
    void onDeviceWorkerLog(const QString &msg, int category);
    void onDeviceWorkerPollResult(int deviceId, const QString &deviceName,
                                  int pollReg, int value);
    void onDeviceWorkerRefresh();
    void onDeviceWorkerDisconnected(int deviceId, const QString &deviceName);
    
    // Dashboard tab slots
    void onConfigureAlarms();
    void onRefreshDashboard();
    void onDarkModeToggled(bool checked);

    // Settings tab slots
    void onOpenManual();
    
    // Log panel slots
    void onClearLog();
    void onSaveLog();
    void onLogFilterChanged(int index);

private:
    void setupUI();
    void createMasterTab();
    void createGatewayTab();
    void createDevicesTab();
    void createDashboardTab();
    void createLogsTab();
    void createSettingsTab();
    
    void log(LogCategory category, const QString &message);
    void addLogMessage(QTextEdit *textEdit, const QString &message);  // For m_textData only
    void updateStatus(const QString &message);
    void enableConnectionControls(bool connected);
    void refreshDeviceList();
    void updateDashboardMetrics();
    
    // UI Components
    QTabWidget *m_tabWidget;
    QLabel *m_statusLabel;
    
    // Master tab widgets
    QComboBox *m_comboMode;
    QLineEdit *m_editIP;
    QLineEdit *m_editPort;
    QComboBox *m_comboCOM;
    QComboBox *m_comboBaud;
    QComboBox *m_comboParity;
    QLineEdit *m_editSlave;
    QPushButton *m_btnConnect;
    QPushButton *m_btnDisconnect;
    
    QComboBox *m_comboFunction;
    QLineEdit *m_editAddress;
    QLineEdit *m_editQuantity;
    QLineEdit *m_editValue;
    QPushButton *m_btnRead;
    QPushButton *m_btnWrite;
    QComboBox *m_comboFormat;
    
    QCheckBox *m_checkPoll;
    QLineEdit *m_editPollInterval;
    QTextEdit *m_textData;  // Register values display
    
    // Gateway tab widgets
    QRadioButton *m_radioRtuToTcp;
    QRadioButton *m_radioTcpToRtu;
    QLineEdit *m_editGatewayIP;
    QLineEdit *m_editGatewayPort;
    QComboBox *m_comboGatewayCOM;
    QComboBox *m_comboGatewayBaud;
    QComboBox *m_comboGatewayParity;
    QPushButton *m_btnStartGateway;
    QPushButton *m_btnStopGateway;
    QTextEdit *m_textGatewayLog;
    
    // Device tab widgets
    QTableWidget *m_tableDevices;
    QPushButton *m_btnAddDevice;
    QPushButton *m_btnRemoveDevice;
    QPushButton *m_btnEditDevice;
    QPushButton *m_btnConnectAll;
    QPushButton *m_btnDisconnectAll;
    QPushButton *m_btnMonitorAll;
    QPushButton *m_btnStopMonitor;
    QPushButton *m_btnScanNetwork;
    QPushButton *m_btnStopScan;
    QLineEdit *m_editScanStartIP;
    QLineEdit *m_editScanEndIP;
    QListWidget *m_listScanResults;
    
    // Dashboard tab widgets
    QLabel *m_metricLabels[6];
    QPushButton *m_btnRefreshDash;
    QPushButton *m_btnConfigureAlarms;
    QCheckBox *m_checkDarkMode;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    PollingGraphWidget *m_pollingGraphWidget;
#endif
    
    // Logs tab widgets
    QTextEdit *m_logView;
    QComboBox *m_comboLogFilter;
    LogCategory m_currentLogFilter;
    QList<QPair<LogCategory, QString>> m_logEntries;
    
    // State variables
    modbus_t *m_modbus;
    bool m_connected;
    bool m_polling;
    int m_displayFormat; // 0=Decimal, 1=Hex, 2=Octal
    QTimer *m_pollTimer;

    // Gateway
    bool m_gatewayRunning;
    bool m_gatewayIsRtuToTcp;
    GatewayThread *m_gatewayThread;
    
    // Device monitoring
    bool m_monitoringAll;
    DeviceWorkerThread *m_deviceWorker;
    bool m_scanningNetwork;
    QTimer *m_scanTimer;
    quint32 m_scanCurrentIP;
    quint32 m_scanEndIP;
    int m_scanPort;
    
    friend class GatewayThread;
};

// Gateway thread class
class GatewayThread : public QThread {
    Q_OBJECT
    
public:
    explicit GatewayThread(MainWindow *parent);
    ~GatewayThread();
    
    void setParameters(bool isRtuToTcp, const QString &tcpIP, int tcpPort,
                      const QString &comPort, int baudRate, char parity);
    void stop();
    
signals:
    void logMessage(const QString &message);
    
protected:
    void run() override;
    
private:
    MainWindow *m_mainWindow;
    bool m_shouldStop;
    bool m_isRtuToTcp;
    QString m_tcpIP;
    int m_tcpPort;
    QString m_comPort;
    int m_baudRate;
    char m_parity;
    
    modbus_t *m_gatewayTcp;
    modbus_t *m_gatewayRtu;
    qintptr   m_serverSock;  /* listen socket handle for RTU-to-TCP mode */
};

/* ═══════════════════════════════════════════════════════════════════════
   DeviceWorkerThread — runs all device I/O off the UI thread
   Handles: connect-all, disconnect-all, polling + auto-reconnect
   ═══════════════════════════════════════════════════════════════════════ */
class DeviceWorkerThread : public QThread {
    Q_OBJECT

public:
    enum Command { CMD_NONE, CMD_CONNECT_ALL, CMD_DISCONNECT_ALL };

    explicit DeviceWorkerThread(QObject *parent = nullptr);
    ~DeviceWorkerThread();

    void startMonitoring(int intervalMs = 1000);
    void stopMonitoring();
    void requestConnectAll();
    void requestDisconnectAll();

signals:
    // All signals are delivered to the UI thread via queued connections.
    void deviceLog(const QString &msg, int category);
    void pollResult(int deviceId, const QString &deviceName,
                    int pollReg, int value);
    void deviceDisconnected(int deviceId, const QString &deviceName);
    void requestRefresh();       // ask UI to call refreshDeviceList()

protected:
    void run() override;

private:
    QAtomicInt m_shouldStop;
    QAtomicInt m_command;        // pending one-shot command
    int m_intervalMs;
};

#endif // MAINWINDOW_H
