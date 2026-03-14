#include <QMetaObject>
#include "mainwindow.h"
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "pollinggraphwidget.h"
#endif
#include "alarm_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHeaderView>
#include <QSplitter>
#include <QApplication>
#include <QDateTime>
#include <QStatusBar>
#include <QIcon>
#include <QIntValidator>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <cstring>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_modbus(nullptr)
    , m_connected(false)
    , m_polling(false)
    , m_displayFormat(0)
    , m_gatewayRunning(false)
    , m_gatewayIsRtuToTcp(true)
    , m_gatewayThread(nullptr)
    , m_monitoringAll(false)
    , m_scanningNetwork(false)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    , m_pollingGraphWidget(nullptr)
#endif
{
    // Initialize configuration
    config_init();
    config_load_default();
    
    // Initialize application logger
    app_logger_config_t app_log_config = {0};
    app_log_config.enabled = true;
    app_log_config.log_to_file = true;
    app_log_config.log_to_console = false;
    app_log_config.min_level = LOG_LEVEL_INFO;
    strcpy(app_log_config.log_directory, "logs");
    strcpy(app_log_config.log_filename, "modbushub");
    app_log_config.max_file_size_mb = 50;
    app_log_config.rotate_on_startup = true;
    app_logger_init(&app_log_config);
    
    APP_LOG_INFO("Application", "ModbusHub starting up");
    
    // Initialize device manager
    device_manager_init();
    APP_LOG_INFO("DeviceManager", "Device manager initialized");

    // Restore previously commissioned devices from backup file
    int restoredDevices = device_manager_load("devices.ini");
    if (restoredDevices > 0) {
        APP_LOG_INFO("DeviceManager", "Restored %d device(s) from backup", restoredDevices);
    } else {
        APP_LOG_INFO("DeviceManager", "No device backup found – starting with empty device list");
    }
    
    // Initialize alarm manager
    alarm_manager_init();
    APP_LOG_INFO("AlarmManager", "Alarm manager initialized");
    
    // Register alarm event callback (for status monitor log)
    alarm_manager_set_event_callback(&MainWindow::alarmEventCallback);
    
    // Initialize packet monitor
    packet_monitor_init();
    
    // Initialize data logger
    logger_config_t log_config = {0};
    log_config.enabled = g_app_config.logger_enabled;
    log_config.log_on_change_only = g_app_config.log_on_change_only;
    log_config.interval_ms = g_app_config.log_interval_ms;
    log_config.max_file_size_mb = g_app_config.log_max_file_size_mb;
    log_config.retention_days = g_app_config.log_retention_days;
    strcpy(log_config.log_directory, g_app_config.log_directory);
    strcpy(log_config.filename_prefix, "modbus_data");
    log_config.include_milliseconds = true;
    log_config.compress_old_logs = false;
    data_logger_init(&log_config);
    
    // Setup timers
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);
    
    m_deviceWorker = nullptr;

    m_scanTimer = new QTimer(this);
    connect(m_scanTimer, &QTimer::timeout, this, &MainWindow::onScanTimer);
    m_scanningNetwork = false;
    
    setupUI();

    // Populate device table with any devices restored from backup
    refreshDeviceList();
    if (restoredDevices > 0) {
        log(LOG_CAT_SYSTEM, QString("[STARTUP] Restored %1 commissioned device(s) from backup").arg(restoredDevices));
    }

    setWindowTitle("ModbusHub - TCP/RTU Master");
    setWindowIcon(QIcon(":/icons/icons/app_logo.svg"));
    resize(1200, 800);
}

MainWindow::~MainWindow() {
    // Persist commissioned devices so they survive a restart
    device_manager_save("devices.ini");
    APP_LOG_INFO("Application", "Device configuration saved to devices.ini");

    // Stop polling
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
    
    // Stop monitoring
    if (m_deviceWorker) {
        m_deviceWorker->stopMonitoring();
        m_deviceWorker->wait();
        delete m_deviceWorker;
        m_deviceWorker = nullptr;
    }
    
    // Disconnect modbus
    if (m_modbus) {
        modbus_close(m_modbus);
        modbus_free(m_modbus);
    }
    
    // Stop gateway
    if (m_gatewayThread && m_gatewayThread->isRunning()) {
        m_gatewayThread->stop();
        m_gatewayThread->wait();
        delete m_gatewayThread;
    }
    
    // Cleanup (these functions may not exist in all backend implementations)
    // data_logger_cleanup();
    // packet_monitor_cleanup();
    // device_manager_cleanup();
    // app_logger_cleanup();
}

// Static C callback for alarm events
void MainWindow::alarmEventCallback(const char* message, int device_id, int register_address, int value, int threshold) {
    // Forward to instance method using Qt event loop (thread-safe)
    MainWindow* win = nullptr;
    static MainWindow* s_instance = nullptr;
    if (!s_instance) {
        // Try to find the main window instance
        QWidgetList topLevels = QApplication::topLevelWidgets();
        for (QWidget* w : topLevels) {
            win = qobject_cast<MainWindow*>(w);
            if (win) break;
        }
        s_instance = win;
    } else {
        win = s_instance;
    }
    if (win) {
        QString msg = QString::fromUtf8(message);
        QMetaObject::invokeMethod(win, [win, msg, device_id, register_address, value, threshold]() {
            win->handleAlarmEvent(msg, device_id, register_address, value, threshold);
        }, Qt::QueuedConnection);
    }
}

// Instance method to handle alarm event and log to status monitor
void MainWindow::handleAlarmEvent(const QString& message, int device_id, int register_address, int value, int threshold) {
    Q_UNUSED(device_id);
    Q_UNUSED(register_address);
    Q_UNUSED(value);
    Q_UNUSED(threshold);
    log(LOG_CAT_SYSTEM, QString("[ALARM] %1").arg(message));
}

void MainWindow::setupUI() {
    // Create central widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);
    
    // Create tabs
    createMasterTab();
    createGatewayTab();
    createDevicesTab();
    createDashboardTab();
    createLogsTab();
    createSettingsTab();
    
    // Initialize mode-dependent field states (TCP is default)
    onModeChanged(0);
    
    // Status bar
    m_statusLabel = new QLabel("Ready", this);
    statusBar()->addWidget(m_statusLabel, 1);
    
    updateStatus("Ready");
}

void MainWindow::createMasterTab() {
    QWidget *masterTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(masterTab);
    
    // Connection group
    QGroupBox *connGroup = new QGroupBox("Connection Settings");
    QVBoxLayout *connLayout = new QVBoxLayout();
    
    // Define label width for alignment
    const int labelWidth = 75;
    
    // Mode selection
    QHBoxLayout *modeLayout = new QHBoxLayout();
    QLabel *lblMode = new QLabel("Mode:");
    lblMode->setMinimumWidth(labelWidth);
    modeLayout->addWidget(lblMode);
    m_comboMode = new QComboBox();
    m_comboMode->addItems({"Modbus TCP", "Modbus RTU"});
    connect(m_comboMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    modeLayout->addWidget(m_comboMode);
    modeLayout->addStretch();
    connLayout->addLayout(modeLayout);
    
    // TCP settings
    QHBoxLayout *tcpLayout = new QHBoxLayout();
    QLabel *lblIP = new QLabel("IP Address:");
    lblIP->setMinimumWidth(labelWidth);
    tcpLayout->addWidget(lblIP);
    m_editIP = new QLineEdit("127.0.0.1");
    tcpLayout->addWidget(m_editIP);
    QLabel *lblPort = new QLabel("Port:");
    lblPort->setMinimumWidth(40);
    tcpLayout->addWidget(lblPort);
    m_editPort = new QLineEdit("502");
    m_editPort->setMaximumWidth(80);
    tcpLayout->addWidget(m_editPort);
    tcpLayout->addStretch();
    connLayout->addLayout(tcpLayout);
    
    // RTU settings
    QHBoxLayout *rtuLayout = new QHBoxLayout();
    QLabel *lblCOM = new QLabel("COM Port:");
    lblCOM->setMinimumWidth(labelWidth);
    rtuLayout->addWidget(lblCOM);
    m_comboCOM = new QComboBox();
    m_comboCOM->setEditable(true);
#ifdef _WIN32
    m_comboCOM->addItems({"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8"});
#else
    m_comboCOM->addItems({"/dev/ttyS0", "/dev/ttyS1", "/dev/ttyUSB0", "/dev/ttyUSB1"});
#endif
    m_comboCOM->setMinimumWidth(120);
    rtuLayout->addWidget(m_comboCOM);
    
    QLabel *lblBaud = new QLabel("Baud Rate:");
    lblBaud->setMinimumWidth(70);
    rtuLayout->addWidget(lblBaud);
    m_comboBaud = new QComboBox();
    m_comboBaud->setEditable(true);
    m_comboBaud->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_comboBaud->setCurrentText("9600");
    m_comboBaud->setValidator(new QIntValidator(300, 4000000, this));
    m_comboBaud->setMinimumWidth(100);
    rtuLayout->addWidget(m_comboBaud);
    
    QLabel *lblParity = new QLabel("Parity:");
    lblParity->setMinimumWidth(50);
    rtuLayout->addWidget(lblParity);
    m_comboParity = new QComboBox();
    m_comboParity->addItems({"None", "Even", "Odd"});
    m_comboParity->setMinimumWidth(80);
    rtuLayout->addWidget(m_comboParity);
    rtuLayout->addStretch();
    connLayout->addLayout(rtuLayout);
    
    // Slave ID
    QHBoxLayout *slaveLayout = new QHBoxLayout();
    QLabel *lblSlave = new QLabel("Slave ID:");
    lblSlave->setMinimumWidth(labelWidth);
    slaveLayout->addWidget(lblSlave);
    m_editSlave = new QLineEdit("1");
    m_editSlave->setMaximumWidth(80);
    slaveLayout->addWidget(m_editSlave);
    slaveLayout->addStretch();
    connLayout->addLayout(slaveLayout);
    
    // Connection buttons
    QHBoxLayout *connBtnLayout = new QHBoxLayout();
    m_btnConnect = new QPushButton(QIcon(":/icons/icons/connect.svg"), "Connect");
    m_btnDisconnect = new QPushButton(QIcon(":/icons/icons/disconnect.svg"), "Disconnect");
    m_btnDisconnect->setEnabled(false);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connBtnLayout->addWidget(m_btnConnect);
    connBtnLayout->addWidget(m_btnDisconnect);
    connBtnLayout->addStretch();
    connLayout->addLayout(connBtnLayout);
    
    connGroup->setLayout(connLayout);
    mainLayout->addWidget(connGroup);
    
    // Operations group
    QGroupBox *opsGroup = new QGroupBox("Modbus Operations");
    QVBoxLayout *opsLayout = new QVBoxLayout();
    
    QHBoxLayout *funcLayout = new QHBoxLayout();
    QLabel *lblFunc = new QLabel("Function:");
    lblFunc->setMinimumWidth(labelWidth);
    funcLayout->addWidget(lblFunc);
    m_comboFunction = new QComboBox();
    m_comboFunction->addItems({
        "Read Coils (0x01)",
        "Read Discrete Inputs (0x02)",
        "Read Holding Registers (0x03)",
        "Read Input Registers (0x04)",
        "Write Single Coil (0x05)",
        "Write Single Register (0x06)",
        "Write Multiple Coils (0x0F)",
        "Write Multiple Registers (0x10)"
    });
    m_comboFunction->setCurrentIndex(2); // Default to Read Holding Registers
    connect(m_comboFunction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFunctionChanged);
    funcLayout->addWidget(m_comboFunction, 1);
    opsLayout->addLayout(funcLayout);
    
    QHBoxLayout *addrLayout = new QHBoxLayout();
    QLabel *lblAddr = new QLabel("Address:");
    lblAddr->setMinimumWidth(labelWidth);
    addrLayout->addWidget(lblAddr);
    m_editAddress = new QLineEdit("0");
    m_editAddress->setMinimumWidth(80);
    m_editAddress->setMaximumWidth(100);
    addrLayout->addWidget(m_editAddress);
    
    QLabel *lblQty = new QLabel("Quantity:");
    lblQty->setMinimumWidth(60);
    addrLayout->addWidget(lblQty);
    m_editQuantity = new QLineEdit("10");
    m_editQuantity->setMinimumWidth(80);
    m_editQuantity->setMaximumWidth(100);
    addrLayout->addWidget(m_editQuantity);
    
    QLabel *lblVal = new QLabel("Value(s):");
    lblVal->setMinimumWidth(60);
    addrLayout->addWidget(lblVal);
    m_editValue = new QLineEdit("100");
    m_editValue->setMinimumWidth(100);
    addrLayout->addWidget(m_editValue);
    
    QLabel *lblFmt = new QLabel("Format:");
    lblFmt->setMinimumWidth(50);
    addrLayout->addWidget(lblFmt);
    m_comboFormat = new QComboBox();
    m_comboFormat->setMinimumWidth(100);
    m_comboFormat->addItems({"Decimal", "Hexadecimal", "Octal"});
    connect(m_comboFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFormatChanged);
    addrLayout->addWidget(m_comboFormat);
    
    opsLayout->addLayout(addrLayout);
    
    QHBoxLayout *opsBtnLayout = new QHBoxLayout();
    m_btnRead = new QPushButton(QIcon(":/icons/icons/read.svg"), "Read");
    m_btnWrite = new QPushButton(QIcon(":/icons/icons/write.svg"), "Write");
    m_btnRead->setEnabled(false);
    m_btnWrite->setEnabled(false);
    connect(m_btnRead, &QPushButton::clicked, this, &MainWindow::onRead);
    connect(m_btnWrite, &QPushButton::clicked, this, &MainWindow::onWrite);
    opsBtnLayout->addWidget(m_btnRead);
    opsBtnLayout->addWidget(m_btnWrite);
    
    // Polling
    m_checkPoll = new QCheckBox("Auto-Poll");
    connect(m_checkPoll, &QCheckBox::toggled, this, &MainWindow::onPollToggled);
    opsBtnLayout->addWidget(m_checkPoll);
    
    opsBtnLayout->addWidget(new QLabel("Interval (ms):"));
    m_editPollInterval = new QLineEdit("1000");
    m_editPollInterval->setMaximumWidth(80);
    opsBtnLayout->addWidget(m_editPollInterval);
    
    opsBtnLayout->addStretch();
    opsLayout->addLayout(opsBtnLayout);
    
    opsGroup->setLayout(opsLayout);
    mainLayout->addWidget(opsGroup);
    
    // Data display - Register values only
    QGroupBox *dataGroup = new QGroupBox("Register Values");
    QVBoxLayout *dataLayout = new QVBoxLayout();
    m_textData = new QTextEdit();
    m_textData->setReadOnly(true);
    m_textData->setFont(QFont("Consolas", 10));
    m_textData->setProperty("class", "logView");
    dataLayout->addWidget(m_textData);
    QPushButton *btnClearData = new QPushButton(QIcon(":/icons/icons/clear.svg"), "Clear");
    connect(btnClearData, &QPushButton::clicked, [this]() { m_textData->clear(); });
    dataLayout->addWidget(btnClearData);
    dataGroup->setLayout(dataLayout);
    mainLayout->addWidget(dataGroup, 1);
    
    m_tabWidget->addTab(masterTab, QIcon(":/icons/icons/master.svg"), "Master");
}

void MainWindow::createGatewayTab() {
    QWidget *gatewayTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(gatewayTab);
    
    QGroupBox *configGroup = new QGroupBox("Gateway Configuration");
    QVBoxLayout *configLayout = new QVBoxLayout();
    
    // Define label width for alignment
    const int gwLabelWidth = 90;
    
    // Mode selection
    QHBoxLayout *modeLayout = new QHBoxLayout();
    m_radioRtuToTcp = new QRadioButton("RTU to TCP");
    m_radioTcpToRtu = new QRadioButton("TCP to RTU");
    m_radioRtuToTcp->setChecked(true);
    connect(m_radioRtuToTcp, &QRadioButton::toggled, this, &MainWindow::onGatewayModeChanged);
    modeLayout->addWidget(m_radioRtuToTcp);
    modeLayout->addWidget(m_radioTcpToRtu);
    modeLayout->addStretch();
    configLayout->addLayout(modeLayout);
    
    // TCP settings
    QHBoxLayout *tcpLayout = new QHBoxLayout();
    QLabel *lblGwIP = new QLabel("TCP Listen IP:");
    lblGwIP->setMinimumWidth(gwLabelWidth);
    tcpLayout->addWidget(lblGwIP);
    m_editGatewayIP = new QLineEdit("0.0.0.0");
    tcpLayout->addWidget(m_editGatewayIP);
    QLabel *lblGwPort = new QLabel("Port:");
    lblGwPort->setMinimumWidth(40);
    tcpLayout->addWidget(lblGwPort);
    m_editGatewayPort = new QLineEdit("502");
    m_editGatewayPort->setMaximumWidth(80);
    tcpLayout->addWidget(m_editGatewayPort);
    tcpLayout->addStretch();
    configLayout->addLayout(tcpLayout);
    
    // RTU settings
    QHBoxLayout *rtuLayout = new QHBoxLayout();
    QLabel *lblGwCOM = new QLabel("COM Port:");
    lblGwCOM->setMinimumWidth(gwLabelWidth);
    rtuLayout->addWidget(lblGwCOM);
    m_comboGatewayCOM = new QComboBox();
    m_comboGatewayCOM->setEditable(true);
    m_comboGatewayCOM->setMinimumWidth(120);
#ifdef _WIN32
    m_comboGatewayCOM->addItems({"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8"});
#else
    m_comboGatewayCOM->addItems({"/dev/ttyS0", "/dev/ttyS1", "/dev/ttyUSB0", "/dev/ttyUSB1"});
#endif
    rtuLayout->addWidget(m_comboGatewayCOM);
    
    QLabel *lblGwBaud = new QLabel("Baud Rate:");
    lblGwBaud->setMinimumWidth(70);
    rtuLayout->addWidget(lblGwBaud);
    m_comboGatewayBaud = new QComboBox();
    m_comboGatewayBaud->setEditable(true);
    m_comboGatewayBaud->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_comboGatewayBaud->setCurrentText("9600");
    m_comboGatewayBaud->setValidator(new QIntValidator(300, 4000000, this));
    m_comboGatewayBaud->setMinimumWidth(100);
    rtuLayout->addWidget(m_comboGatewayBaud);
    
    QLabel *lblGwParity = new QLabel("Parity:");
    lblGwParity->setMinimumWidth(50);
    rtuLayout->addWidget(lblGwParity);
    m_comboGatewayParity = new QComboBox();
    m_comboGatewayParity->addItems({"None", "Even", "Odd"});
    m_comboGatewayParity->setMinimumWidth(80);
    rtuLayout->addWidget(m_comboGatewayParity);
    rtuLayout->addStretch();
    configLayout->addLayout(rtuLayout);
    
    // Control buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_btnStartGateway = new QPushButton(QIcon(":/icons/icons/play.svg"), "Start Gateway");
    m_btnStopGateway = new QPushButton(QIcon(":/icons/icons/stop.svg"), "Stop Gateway");
    m_btnStopGateway->setEnabled(false);
    connect(m_btnStartGateway, &QPushButton::clicked, this, &MainWindow::onStartGateway);
    connect(m_btnStopGateway, &QPushButton::clicked, this, &MainWindow::onStopGateway);
    btnLayout->addWidget(m_btnStartGateway);
    btnLayout->addWidget(m_btnStopGateway);
    btnLayout->addStretch();
    configLayout->addLayout(btnLayout);
    
    configGroup->setLayout(configLayout);
    mainLayout->addWidget(configGroup);
    
    // Gateway Log display
    QGroupBox *logGroup = new QGroupBox("Gateway Log");
    QVBoxLayout *logLayout = new QVBoxLayout();
    m_textGatewayLog = new QTextEdit();
    m_textGatewayLog->setReadOnly(true);
    m_textGatewayLog->setFont(QFont("Consolas", 10));
    m_textGatewayLog->setProperty("class", "logView");
    logLayout->addWidget(m_textGatewayLog);
    QPushButton *btnClearGwLog = new QPushButton(QIcon(":/icons/icons/clear.svg"), "Clear Log");
    connect(btnClearGwLog, &QPushButton::clicked, [this]() { m_textGatewayLog->clear(); });
    logLayout->addWidget(btnClearGwLog);
    logGroup->setLayout(logLayout);
    mainLayout->addWidget(logGroup, 1);
    
    m_tabWidget->addTab(gatewayTab, QIcon(":/icons/icons/gateway.svg"), "Gateway");
}

void MainWindow::createDevicesTab() {
    QWidget *devicesTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(devicesTab);
    
    // Device list
    QGroupBox *listGroup = new QGroupBox("Configured Devices");
    QVBoxLayout *listLayout = new QVBoxLayout();
    
    m_tableDevices = new QTableWidget();
    m_tableDevices->setColumnCount(8);
    m_tableDevices->setHorizontalHeaderLabels({"ID", "Name", "Type", "Connection", "Slave ID", "Poll Interval (ms)", "Enabled", "Status"});
    m_tableDevices->verticalHeader()->setVisible(false);
    m_tableDevices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableDevices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableDevices->setAlternatingRowColors(true);
    m_tableDevices->setSortingEnabled(false);
    // Column widths
    m_tableDevices->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableDevices->setColumnWidth(0, 45);   // ID
    m_tableDevices->setColumnWidth(2, 55);   // Type
    m_tableDevices->setColumnWidth(4, 70);   // Slave ID
    m_tableDevices->setColumnWidth(5, 115);  // Poll Interval
    m_tableDevices->setColumnWidth(6, 65);   // Enabled
    m_tableDevices->setColumnWidth(7, 120);  // Status
    m_tableDevices->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);  // Name stretches
    m_tableDevices->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);  // Connection stretches
    connect(m_tableDevices, &QTableWidget::cellClicked, this, &MainWindow::onDeviceSelected);
    listLayout->addWidget(m_tableDevices);
    
    QHBoxLayout *deviceBtnLayout = new QHBoxLayout();
    m_btnAddDevice = new QPushButton(QIcon(":/icons/icons/add.svg"), "Add Device");
    m_btnRemoveDevice = new QPushButton(QIcon(":/icons/icons/remove.svg"), "Remove Device");
    m_btnEditDevice = new QPushButton(QIcon(":/icons/icons/edit.svg"), "Edit Device");
    m_btnConnectAll = new QPushButton(QIcon(":/icons/icons/connect_all.svg"), "Connect All");
    m_btnDisconnectAll = new QPushButton(QIcon(":/icons/icons/disconnect_all.svg"), "Disconnect All");
    connect(m_btnAddDevice, &QPushButton::clicked, this, &MainWindow::onAddDevice);
    connect(m_btnRemoveDevice, &QPushButton::clicked, this, &MainWindow::onRemoveDevice);
    connect(m_btnEditDevice, &QPushButton::clicked, this, &MainWindow::onEditDevice);
    connect(m_btnConnectAll, &QPushButton::clicked, this, &MainWindow::onConnectAll);
    connect(m_btnDisconnectAll, &QPushButton::clicked, this, &MainWindow::onDisconnectAll);
    deviceBtnLayout->addWidget(m_btnAddDevice);
    deviceBtnLayout->addWidget(m_btnRemoveDevice);
    deviceBtnLayout->addWidget(m_btnEditDevice);
    deviceBtnLayout->addWidget(m_btnConnectAll);
    deviceBtnLayout->addWidget(m_btnDisconnectAll);
    deviceBtnLayout->addStretch();
    listLayout->addLayout(deviceBtnLayout);
    
    listGroup->setLayout(listLayout);
    mainLayout->addWidget(listGroup);
    
    // Monitoring controls
    QGroupBox *monitorGroup = new QGroupBox("Device Monitoring");
    QVBoxLayout *monitorLayout = new QVBoxLayout();
    
    QHBoxLayout *monitorBtnLayout = new QHBoxLayout();
    m_btnMonitorAll = new QPushButton(QIcon(":/icons/icons/monitor.svg"), "Monitor All");
    m_btnStopMonitor = new QPushButton(QIcon(":/icons/icons/stop.svg"), "Stop Monitoring");
    m_btnStopMonitor->setEnabled(false);
    connect(m_btnMonitorAll, &QPushButton::clicked, this, &MainWindow::onMonitorAll);
    connect(m_btnStopMonitor, &QPushButton::clicked, this, &MainWindow::onStopMonitor);
    monitorBtnLayout->addWidget(m_btnMonitorAll);
    monitorBtnLayout->addWidget(m_btnStopMonitor);
    monitorBtnLayout->addStretch();
    monitorLayout->addLayout(monitorBtnLayout);
    
    monitorGroup->setLayout(monitorLayout);
    mainLayout->addWidget(monitorGroup);
    
    // Network scanner
    QGroupBox *scanGroup = new QGroupBox("Network Scanner");
    QVBoxLayout *scanLayout = new QVBoxLayout();
    
    QHBoxLayout *scanControlLayout = new QHBoxLayout();
    QLabel *lblStartIP = new QLabel("Start IP:");
    lblStartIP->setMinimumWidth(55);
    scanControlLayout->addWidget(lblStartIP);
    m_editScanStartIP = new QLineEdit("192.168.1.1");
    m_editScanStartIP->setMinimumWidth(120);
    scanControlLayout->addWidget(m_editScanStartIP);
    QLabel *lblEndIP = new QLabel("End IP:");
    lblEndIP->setMinimumWidth(50);
    scanControlLayout->addWidget(lblEndIP);
    m_editScanEndIP = new QLineEdit("192.168.1.254");
    m_editScanEndIP->setMinimumWidth(120);
    scanControlLayout->addWidget(m_editScanEndIP);
    m_btnScanNetwork = new QPushButton(QIcon(":/icons/icons/search.svg"), "Scan Network");
    m_btnStopScan = new QPushButton(QIcon(":/icons/icons/stop.svg"), "Stop Scan");
    m_btnStopScan->setEnabled(false);
    connect(m_btnScanNetwork, &QPushButton::clicked, this, &MainWindow::onScanNetwork);
    connect(m_btnStopScan, &QPushButton::clicked, this, &MainWindow::onStopScan);
    scanControlLayout->addWidget(m_btnScanNetwork);
    scanControlLayout->addWidget(m_btnStopScan);
    scanControlLayout->addStretch();
    scanLayout->addLayout(scanControlLayout);
    
    m_listScanResults = new QListWidget();
    scanLayout->addWidget(m_listScanResults);
    
    scanGroup->setLayout(scanLayout);
    mainLayout->addWidget(scanGroup);
    
    m_tabWidget->addTab(devicesTab, QIcon(":/icons/icons/devices.svg"), "Devices");
    
    // Refresh device list
    refreshDeviceList();
}

void MainWindow::createDashboardTab() {
    QWidget *dashboardTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(dashboardTab);
    
    // Metrics display
    QGroupBox *metricsGroup = new QGroupBox("System Metrics");
    QGridLayout *metricsLayout = new QGridLayout();
    
    for (int i = 0; i < 6; i++) {
        m_metricLabels[i] = new QLabel("Metric " + QString::number(i + 1) + ": --");
        m_metricLabels[i]->setObjectName("metricLabel");
        m_metricLabels[i]->setProperty("class", "metric-card");
        metricsLayout->addWidget(m_metricLabels[i], i / 3, i % 3);
    }
    
    metricsGroup->setLayout(metricsLayout);
    mainLayout->addWidget(metricsGroup);
    
    // Polling graph (Qt6 only)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QGroupBox *graphGroup = new QGroupBox("Polling Graph – Register Values vs Time");
    QVBoxLayout *graphLayout = new QVBoxLayout();
    m_pollingGraphWidget = new PollingGraphWidget();
    graphLayout->addWidget(m_pollingGraphWidget);
    graphGroup->setLayout(graphLayout);
    mainLayout->addWidget(graphGroup, 3);  // give graph most of the space
#endif

    // Alarm configuration
    QGroupBox *alarmGroup = new QGroupBox("Alarms");
    QVBoxLayout *alarmLayout = new QVBoxLayout();
    
    QHBoxLayout *alarmBtnLayout = new QHBoxLayout();
    m_btnConfigureAlarms = new QPushButton(QIcon(":/icons/icons/alarm.svg"), "Configure Alarms");
    m_btnRefreshDash = new QPushButton(QIcon(":/icons/icons/refresh.svg"), "Refresh Dashboard");
    connect(m_btnConfigureAlarms, &QPushButton::clicked, this, &MainWindow::onConfigureAlarms);
    connect(m_btnRefreshDash, &QPushButton::clicked, this, &MainWindow::onRefreshDashboard);
    alarmBtnLayout->addWidget(m_btnConfigureAlarms);
    alarmBtnLayout->addWidget(m_btnRefreshDash);
    alarmBtnLayout->addStretch();
    alarmLayout->addLayout(alarmBtnLayout);
    
    alarmGroup->setLayout(alarmLayout);
    mainLayout->addWidget(alarmGroup);

    m_tabWidget->addTab(dashboardTab, QIcon(":/icons/icons/dashboard.svg"), "Dashboard");
    updateDashboardMetrics();
}

void MainWindow::createLogsTab() {
    QWidget *logsTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(logsTab);
    
    // Filter and controls group
    QGroupBox *filterGroup = new QGroupBox("Log Filter");
    QHBoxLayout *filterLayout = new QHBoxLayout();
    
    QLabel *lblFilter = new QLabel("Category:");
    lblFilter->setMinimumWidth(70);
    filterLayout->addWidget(lblFilter);
    
    m_comboLogFilter = new QComboBox();
    m_comboLogFilter->addItem("All Logs", LOG_CAT_ALL);
    m_comboLogFilter->addItem("Master", LOG_CAT_MASTER);
    m_comboLogFilter->addItem("Gateway", LOG_CAT_GATEWAY);
    m_comboLogFilter->addItem("Devices", LOG_CAT_DEVICES);
    m_comboLogFilter->addItem("System", LOG_CAT_SYSTEM);
    m_comboLogFilter->setMinimumWidth(150);
    connect(m_comboLogFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::onLogFilterChanged);
    filterLayout->addWidget(m_comboLogFilter);
    
    filterLayout->addStretch();
    
    QPushButton *btnSaveLog = new QPushButton(QIcon(":/icons/icons/save.svg"), "Save Log");
    connect(btnSaveLog, &QPushButton::clicked, this, &MainWindow::onSaveLog);
    filterLayout->addWidget(btnSaveLog);

    QPushButton *btnClearLog = new QPushButton(QIcon(":/icons/icons/clear.svg"), "Clear Log");
    connect(btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLog);
    filterLayout->addWidget(btnClearLog);
    
    filterGroup->setLayout(filterLayout);
    mainLayout->addWidget(filterGroup);
    
    // Log view group
    QGroupBox *logGroup = new QGroupBox("Application Log");
    QVBoxLayout *logLayout = new QVBoxLayout();
    
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setFont(QFont("Consolas", 10));
    m_logView->setProperty("class", "logView");
    logLayout->addWidget(m_logView);
    
    logGroup->setLayout(logLayout);
    mainLayout->addWidget(logGroup, 1);
    
    m_tabWidget->addTab(logsTab, QIcon(":/icons/icons/logs.svg"), "Logs");
    
    m_currentLogFilter = LOG_CAT_ALL;
}

void MainWindow::createSettingsTab() {
    QWidget *settingsTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(settingsTab);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // ── Appearance ──────────────────────────────────────────────────────────
    QGroupBox *appearanceGroup = new QGroupBox("Appearance");
    QVBoxLayout *appearanceLayout = new QVBoxLayout();
    appearanceLayout->setSpacing(10);

    QHBoxLayout *darkRow = new QHBoxLayout();
    QLabel *darkIcon = new QLabel();
    darkIcon->setPixmap(QIcon(":/icons/icons/settings.svg").pixmap(18, 18));
    darkIcon->setFixedSize(24, 24);
    darkIcon->setAlignment(Qt::AlignCenter);

    m_checkDarkMode = new QCheckBox("Enable Dark Mode");
    m_checkDarkMode->setToolTip(
        "Toggle between the light (macOS-inspired) and dark colour scheme.\n"
        "The change is applied immediately to the entire application.");
    connect(m_checkDarkMode, &QCheckBox::toggled, this, &MainWindow::onDarkModeToggled);
    darkRow->addWidget(darkIcon);
    darkRow->addWidget(m_checkDarkMode);
    darkRow->addStretch();

    QLabel *darkDesc = new QLabel(
        "Switches the application colour scheme between Light and Dark.\n"
        "Light theme: macos_style.qss   |   Dark theme: macos_style_dark.qss");
    darkDesc->setWordWrap(true);
    darkDesc->setStyleSheet("color: #888888; font-size: 11px; margin-left: 28px;");

    appearanceLayout->addLayout(darkRow);
    appearanceLayout->addWidget(darkDesc);
    appearanceGroup->setLayout(appearanceLayout);
    mainLayout->addWidget(appearanceGroup);

    // ── User Manual ─────────────────────────────────────────────────────────
    QGroupBox *manualGroup = new QGroupBox("User Manual");
    QVBoxLayout *manualLayout = new QVBoxLayout();
    manualLayout->setSpacing(10);

    QLabel *manualDesc = new QLabel(
        "Full documentation covering every application tab, Modbus protocol guide,\n"
        "function code reference, packet frame diagrams, and keyboard shortcuts.\n"
        "Click the button below to open the manual in your default web browser.");
    manualDesc->setWordWrap(true);

    QPushButton *btnOpenManual = new QPushButton(
        QIcon(":/icons/icons/manual.svg"), "  Open User Manual");
    btnOpenManual->setMinimumHeight(44);
    btnOpenManual->setMinimumWidth(200);
    btnOpenManual->setIconSize(QSize(22, 22));
    btnOpenManual->setToolTip("Opens the complete application manual in your default web browser.");
    connect(btnOpenManual, &QPushButton::clicked, this, &MainWindow::onOpenManual);

    manualLayout->addWidget(manualDesc);
    manualLayout->addSpacing(4);
    manualLayout->addWidget(btnOpenManual, 0, Qt::AlignLeft);
    manualGroup->setLayout(manualLayout);
    mainLayout->addWidget(manualGroup);

    mainLayout->addStretch();
    m_tabWidget->addTab(settingsTab, QIcon(":/icons/icons/settings.svg"), "Settings");
}

void MainWindow::log(LogCategory category, const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString categoryStr;
    
    switch (category) {
        case LOG_CAT_MASTER:  categoryStr = "[MASTER]";  break;
        case LOG_CAT_GATEWAY: categoryStr = "[GATEWAY]"; break;
        case LOG_CAT_DEVICES: categoryStr = "[DEVICES]"; break;
        case LOG_CAT_SYSTEM:  categoryStr = "[SYSTEM]";  break;
        default:              categoryStr = "[LOG]";     break;
    }
    
    QString formattedMsg = QString("[%1] %2 %3").arg(timestamp, categoryStr, message);
    
    // Store the log entry with its category
    m_logEntries.append(qMakePair(category, formattedMsg));
    
    // Only display if matches current filter or filter is ALL
    if (m_currentLogFilter == LOG_CAT_ALL || m_currentLogFilter == category) {
        m_logView->append(formattedMsg);
    }
    
    // Also display gateway logs in the Gateway tab
    if (category == LOG_CAT_GATEWAY && m_textGatewayLog) {
        m_textGatewayLog->append(formattedMsg);
    }
    
    // Limit stored entries
    while (m_logEntries.size() > 1000) {
        m_logEntries.removeFirst();
    }
}

void MainWindow::onClearLog() {
    m_logView->clear();
    m_logEntries.clear();
}

void MainWindow::onSaveLog() {
    // Build a default filename with the current date/time
    QString defaultName = QString("modbus_log_%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Log File"),
        QDir::homePath() + "/" + defaultName,
        tr("Text Files (*.txt);;CSV Files (*.csv);;All Files (*)"));

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Log"),
            tr("Could not open file for writing:\n%1").arg(filePath));
        return;
    }

    QTextStream out(&file);
    // Header
    out << "# ModbusHub Application Log\n";
    out << "# Exported: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "# Total entries: " << m_logEntries.size() << "\n\n";

    // Entries matching the active filter
    for (const auto& entry : m_logEntries) {
        if (m_currentLogFilter == LOG_CAT_ALL || m_currentLogFilter == entry.first) {
            out << entry.second << "\n";
        }
    }

    file.close();
    log(LOG_CAT_SYSTEM, QString("[LOG] Log exported to: %1").arg(filePath));
    QMessageBox::information(this, tr("Save Log"),
        tr("Log saved successfully to:\n%1").arg(filePath));
}

void MainWindow::onLogFilterChanged(int index) {
    m_currentLogFilter = static_cast<LogCategory>(m_comboLogFilter->itemData(index).toInt());
    
    // Rebuild log view with filtered entries
    m_logView->clear();
    for (const auto& entry : m_logEntries) {
        if (m_currentLogFilter == LOG_CAT_ALL || m_currentLogFilter == entry.first) {
            m_logView->append(entry.second);
        }
    }
}

// Implementation of slots and helper methods continues in next part...
