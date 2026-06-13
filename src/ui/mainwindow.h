#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QString>
#include <QVector>
#include <QMainWindow>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>
#include <QTimer>
#include <QThread>

extern "C" {
    #include "modbus.h"
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


// Log categories for filtering
enum LogCategory {
    LOG_CAT_ALL = 0,
    LOG_CAT_MASTER,
    LOG_CAT_GATEWAY,
    LOG_CAT_SCANNER,
    LOG_CAT_SYSTEM
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    void onTypeChanged(int index);
    void onWordOrderChanged(int index);

    // Gateway tab slots
    void onGatewayModeChanged(bool checked);
    void onStartGateway();
    void onStopGateway();

    // Network scanner tab slots
    void onScanNetwork();
    void onStopScan();
    void onScanResultFound(const QString &ip, int port, int latencyMs);
    void onScanProgress(int current, int total);
    void onScanFinished(int found);
    void onScanResultDoubleClicked(int row, int column);

    // Settings tab slots
    void onDarkModeToggled(bool checked);
    void onOpenManual();

    // Log panel slots
    void onClearLog();
    void onSaveLog();
    void onLogFilterChanged(int index);

private:
    void setupUI();
    void createMasterTab();
    void createGatewayTab();
    void createScannerTab();
    void createLogsTab();
    void createSettingsTab();

    void log(LogCategory category, const QString &message);
    void addLogMessage(QTextEdit *textEdit, const QString &message);  // For m_textData only
    void updateStatus(const QString &message);
    void enableConnectionControls(bool connected);

    // Render the cached read result (m_lastRegs / m_lastBits) into m_textData
    // using the current Type / Word Order / Format selection. Called by onRead
    // and by onTypeChanged / onWordOrderChanged / onFormatChanged so the user
    // can re-interpret the same data without re-fetching from the slave.
    void renderDataView();

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
    QComboBox *m_comboType;       // UInt16/Int16/UInt32/Int32/Float32/Float64
    QComboBox *m_comboWordOrder;  // ABCD/CDAB/BADC/DCBA (multi-register types only)

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

    // Network scanner tab widgets
    QPushButton *m_btnScanNetwork;
    QPushButton *m_btnStopScan;
    QLineEdit *m_editScanStartIP;
    QLineEdit *m_editScanEndIP;
    QLineEdit *m_editScanPort;
    QTableWidget *m_tableScanResults;
    QProgressBar *m_scanProgressBar;
    QLabel *m_scanStatusLabel;

    // Settings tab widgets
    QCheckBox *m_checkDarkMode;

    // Status bar sections
    QLabel *m_statusDot;    // colored connection-state indicator
    QLabel *m_statusTime;   // right-side timestamp

    // Logs tab widgets
    QTextEdit *m_logView;
    QComboBox *m_comboLogFilter;
    LogCategory m_currentLogFilter;
    QList<QPair<LogCategory, QString>> m_logEntries;

    // State variables
    modbus_t *m_modbus;
    bool m_connected;
    bool m_polling;
    int m_displayFormat; // 0=Decimal, 1=Hex, 2=Octal (radix for integer types)
    int m_dataType;      // 0=UInt16, 1=Int16, 2=UInt32, 3=Int32, 4=Float32, 5=Float64
    int m_wordOrder;     // 0=ABCD, 1=CDAB, 2=BADC, 3=DCBA
    QTimer *m_pollTimer;

    // Cache of the last successful read so type/format/word-order changes can
    // re-render without re-fetching. Exactly one of m_lastRegs / m_lastBits is
    // populated; the other is empty.
    QVector<uint16_t> m_lastRegs;
    QVector<uint8_t>  m_lastBits;
    int m_lastReadFunc;     // funcIndex: 0..3
    int m_lastReadAddress;  // start address from the read

    // Gateway
    bool m_gatewayRunning;
    bool m_gatewayIsRtuToTcp;
    GatewayThread *m_gatewayThread;

    // Network scanner
    bool m_scanningNetwork;
    class ScanWorkerThread *m_scanWorker;

    friend class GatewayThread;
    friend class ScanWorkerThread;
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

// ───────────────────────────────────────────────────────────────────────────
// ScanWorkerThread — runs IP range scan off the UI thread
// Emits signals for each found device and for progress updates.
// ───────────────────────────────────────────────────────────────────────────
class ScanWorkerThread : public QThread {
    Q_OBJECT

public:
    explicit ScanWorkerThread(QObject *parent = nullptr);

    void startScan(const QString &startIP, const QString &endIP, int port,
                   int timeoutMs = 200);
    void stop();

signals:
    void resultFound(const QString &ip, int port, int latencyMs);
    void progress(int current, int total);
    void finished(int found);

protected:
    void run() override;

private:
    QAtomicInt m_shouldStop;
    QString m_startIP;
    QString m_endIP;
    int m_port;
    int m_timeoutMs;
};

#endif // MAINWINDOW_H
