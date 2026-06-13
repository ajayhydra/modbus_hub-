#include <QMetaObject>
#include "mainwindow.h"
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
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
    , m_dataType(0)
    , m_wordOrder(0)
    , m_lastReadFunc(-1)
    , m_lastReadAddress(0)
    , m_gatewayRunning(false)
    , m_gatewayIsRtuToTcp(true)
    , m_gatewayThread(nullptr)
    , m_scanningNetwork(false)
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

    m_scanWorker = nullptr;
    m_scanningNetwork = false;

    setupUI();

    setWindowTitle("ModbusHub — Modbus TCP/RTU Master");
    setWindowIcon(QIcon(":/icons/icons/app_logo.svg"));
    setMinimumSize(1100, 700);
    resize(1280, 860);
}

MainWindow::~MainWindow() {
    // Stop polling
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }

    // Stop any running network scan
    if (m_scanWorker) {
        if (m_scanWorker->isRunning()) {
            m_scanWorker->stop();
            m_scanWorker->wait();
        }
        delete m_scanWorker;
        m_scanWorker = nullptr;
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
    // app_logger_cleanup();
}

void MainWindow::setupUI() {
    // Create central widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setIconSize(QSize(20, 20));
    m_tabWidget->setDocumentMode(true);
    mainLayout->addWidget(m_tabWidget);
    
    // Create tabs
    createMasterTab();
    createGatewayTab();
    createScannerTab();
    createLogsTab();
    createSettingsTab();
    
    // Initialize mode-dependent field states (TCP is default)
    onModeChanged(0);
    
    // Status bar — 3 sections: indicator dot | message | timestamp
    m_statusDot = new QLabel(this);
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setFixedSize(10, 10);
    m_statusDot->setToolTip("Connection status");

    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setObjectName("statusText");

    m_statusTime = new QLabel("--:--:--", this);
    m_statusTime->setObjectName("statusTime");

    statusBar()->addWidget(m_statusDot);
    statusBar()->addWidget(new QLabel("  "), 0);   // small gap
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_statusTime);

    updateStatus("Ready");
}

void MainWindow::createMasterTab() {
    QWidget *masterTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(masterTab);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ── Top section: two inner tabs (Connection | Operations) ──
    QTabWidget *topTabs = new QTabWidget();
    topTabs->setMaximumHeight(260);
    topTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto rightLabel = [](const QString& text) {
        QLabel* l = new QLabel(text);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    // ─── Connection tab — full panel width, 3 fields per row ───
    QWidget *connTab = new QWidget();
    QGridLayout *connGrid = new QGridLayout(connTab);
    connGrid->setSpacing(10);
    connGrid->setVerticalSpacing(10);
    connGrid->setContentsMargins(12, 12, 12, 12);
    connGrid->setColumnMinimumWidth(0, 90);   // primary label
    connGrid->setColumnMinimumWidth(2, 80);   // secondary label
    connGrid->setColumnMinimumWidth(4, 70);   // tertiary label
    connGrid->setColumnStretch(1, 3);
    connGrid->setColumnStretch(3, 2);
    connGrid->setColumnStretch(5, 2);

    // Row 0 — Mode (spans all field columns)
    connGrid->addWidget(rightLabel("Mode:"), 0, 0);
    m_comboMode = new QComboBox();
    m_comboMode->addItems({"Modbus TCP", "Modbus RTU"});
    m_comboMode->setMinimumWidth(180);
    connect(m_comboMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    connGrid->addWidget(m_comboMode, 0, 1, 1, 5);

    // Row 1 — IP Address (spans cols 1-3) | Port
    connGrid->addWidget(rightLabel("IP Address:"), 1, 0);
    m_editIP = new QLineEdit("127.0.0.1");
    m_editIP->setMinimumWidth(220);
    connGrid->addWidget(m_editIP, 1, 1, 1, 3);
    connGrid->addWidget(rightLabel("Port:"), 1, 4);
    m_editPort = new QLineEdit("502");
    m_editPort->setMinimumWidth(80);
    connGrid->addWidget(m_editPort, 1, 5);

    // Row 2 — COM | Baud | Parity
    connGrid->addWidget(rightLabel("COM Port:"), 2, 0);
    m_comboCOM = new QComboBox();
    m_comboCOM->setEditable(true);
#ifdef _WIN32
    m_comboCOM->addItems({"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8"});
#else
    m_comboCOM->addItems({"/dev/ttyS0", "/dev/ttyS1", "/dev/ttyUSB0", "/dev/ttyUSB1"});
#endif
    m_comboCOM->setMinimumWidth(140);
    connGrid->addWidget(m_comboCOM, 2, 1);
    connGrid->addWidget(rightLabel("Baud Rate:"), 2, 2);
    m_comboBaud = new QComboBox();
    m_comboBaud->setEditable(true);
    m_comboBaud->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_comboBaud->setCurrentText("9600");
    m_comboBaud->setValidator(new QIntValidator(300, 4000000, this));
    m_comboBaud->setMinimumWidth(120);
    connGrid->addWidget(m_comboBaud, 2, 3);
    connGrid->addWidget(rightLabel("Parity:"), 2, 4);
    m_comboParity = new QComboBox();
    m_comboParity->addItems({"None", "Even", "Odd"});
    m_comboParity->setMinimumWidth(110);
    connGrid->addWidget(m_comboParity, 2, 5);

    // Row 3 — Slave ID + Connect / Disconnect compact, right-aligned
    connGrid->addWidget(rightLabel("Slave ID:"), 3, 0);
    m_editSlave = new QLineEdit("1");
    m_editSlave->setMinimumWidth(80);
    m_editSlave->setMaximumWidth(120);
    connGrid->addWidget(m_editSlave, 3, 1);

    m_btnConnect = new QPushButton(QIcon(":/icons/icons/connect.svg"), "Connect");
    m_btnDisconnect = new QPushButton(QIcon(":/icons/icons/disconnect.svg"), "Disconnect");
    m_btnDisconnect->setEnabled(false);
    m_btnConnect->setFixedSize(96, 28);
    m_btnDisconnect->setFixedSize(96, 28);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    QHBoxLayout *connBtns = new QHBoxLayout();
    connBtns->setSpacing(8);
    connBtns->addStretch();
    connBtns->addWidget(m_btnConnect);
    connBtns->addWidget(m_btnDisconnect);
    connGrid->addLayout(connBtns, 3, 2, 1, 4);

    topTabs->addTab(connTab, QIcon(":/icons/icons/connect.svg"), "Connection");

    // ─── Operations tab — full panel width, 3 fields per row ───
    QWidget *opsTab = new QWidget();
    QVBoxLayout *opsLayout = new QVBoxLayout(opsTab);
    opsLayout->setContentsMargins(12, 12, 12, 12);
    opsLayout->setSpacing(10);
    const int labelWidth = 75;

    // Function row (full width)
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
    m_comboFunction->setCurrentIndex(2);
    connect(m_comboFunction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFunctionChanged);
    funcLayout->addWidget(m_comboFunction, 1);
    opsLayout->addLayout(funcLayout);

    // Address | Quantity | Value(s)
    QHBoxLayout *addrLayout = new QHBoxLayout();
    QLabel *lblAddr = new QLabel("Address:");
    lblAddr->setMinimumWidth(labelWidth);
    addrLayout->addWidget(lblAddr);
    m_editAddress = new QLineEdit("0");
    m_editAddress->setMinimumWidth(80);
    m_editAddress->setMaximumWidth(120);
    addrLayout->addWidget(m_editAddress);

    QLabel *lblQty = new QLabel("Quantity:");
    lblQty->setMinimumWidth(70);
    addrLayout->addWidget(lblQty);
    m_editQuantity = new QLineEdit("10");
    m_editQuantity->setMinimumWidth(80);
    m_editQuantity->setMaximumWidth(120);
    addrLayout->addWidget(m_editQuantity);

    QLabel *lblVal = new QLabel("Value(s):");
    lblVal->setMinimumWidth(70);
    addrLayout->addWidget(lblVal);
    m_editValue = new QLineEdit("100");
    m_editValue->setMinimumWidth(200);
    addrLayout->addWidget(m_editValue, 1);
    opsLayout->addLayout(addrLayout);

    // Type | Word Order | Format
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QLabel *lblType = new QLabel("Type:");
    lblType->setMinimumWidth(labelWidth);
    typeLayout->addWidget(lblType);
    m_comboType = new QComboBox();
    m_comboType->setMinimumWidth(120);
    m_comboType->addItems({"UInt16", "Int16", "UInt32", "Int32", "Float32", "Float64"});
    connect(m_comboType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTypeChanged);
    typeLayout->addWidget(m_comboType, 1);

    QLabel *lblOrder = new QLabel("Word Order:");
    lblOrder->setMinimumWidth(90);
    typeLayout->addWidget(lblOrder);
    m_comboWordOrder = new QComboBox();
    m_comboWordOrder->setMinimumWidth(180);
    m_comboWordOrder->addItems({
        "ABCD (big-endian)",
        "CDAB (word-swap)",
        "BADC (byte-swap)",
        "DCBA (full reverse)"
    });
    m_comboWordOrder->setEnabled(false);
    connect(m_comboWordOrder, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onWordOrderChanged);
    typeLayout->addWidget(m_comboWordOrder, 2);

    QLabel *lblFmt = new QLabel("Format:");
    lblFmt->setMinimumWidth(60);
    typeLayout->addWidget(lblFmt);
    m_comboFormat = new QComboBox();
    m_comboFormat->setMinimumWidth(120);
    m_comboFormat->addItems({"Decimal", "Hexadecimal", "Octal"});
    connect(m_comboFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFormatChanged);
    typeLayout->addWidget(m_comboFormat, 1);
    opsLayout->addLayout(typeLayout);

    // Read | Write | Auto-Poll | Interval (single compact action row, right-aligned)
    m_btnRead = new QPushButton(QIcon(":/icons/icons/read.svg"), "Read");
    m_btnWrite = new QPushButton(QIcon(":/icons/icons/write.svg"), "Write");
    m_btnRead->setEnabled(false);
    m_btnWrite->setEnabled(false);
    m_btnRead->setFixedSize(96, 28);
    m_btnWrite->setFixedSize(96, 28);
    connect(m_btnRead, &QPushButton::clicked, this, &MainWindow::onRead);
    connect(m_btnWrite, &QPushButton::clicked, this, &MainWindow::onWrite);

    m_checkPoll = new QCheckBox("Auto-Poll");
    connect(m_checkPoll, &QCheckBox::toggled, this, &MainWindow::onPollToggled);

    m_editPollInterval = new QLineEdit("1000");
    m_editPollInterval->setMaximumWidth(80);

    QHBoxLayout *opsBtnLayout = new QHBoxLayout();
    opsBtnLayout->setSpacing(8);
    opsBtnLayout->addWidget(m_checkPoll);
    opsBtnLayout->addWidget(new QLabel("Interval (ms):"));
    opsBtnLayout->addWidget(m_editPollInterval);
    opsBtnLayout->addStretch();
    opsBtnLayout->addWidget(m_btnRead);
    opsBtnLayout->addWidget(m_btnWrite);
    opsLayout->addLayout(opsBtnLayout);

    topTabs->addTab(opsTab, QIcon(":/icons/icons/read.svg"), "Operations");

    mainLayout->addWidget(topTabs);

    // ── Register Values — dominant section ──
    QGroupBox *dataGroup = new QGroupBox("Register Values");
    QGridLayout *dataLayout = new QGridLayout();
    dataLayout->setContentsMargins(8, 8, 8, 8);
    dataLayout->setSpacing(0);

    m_textData = new QTextEdit();
    m_textData->setReadOnly(true);
    m_textData->setFont(QFont("Consolas", 11));
    m_textData->setProperty("class", "logView");
    m_textData->setMinimumHeight(400);
    m_textData->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    dataLayout->addWidget(m_textData, 0, 0);

    // Overlay: small icon-only Clear button anchored in the top-right corner
    // of the text area. Both widgets occupy cell (0,0); the button's
    // alignment lets it float over the text area without taking layout space.
    QPushButton *btnClearData = new QPushButton(QIcon(":/icons/icons/clear.svg"), QString());
    btnClearData->setToolTip("Clear register values");
    btnClearData->setFixedSize(28, 24);
    btnClearData->setCursor(Qt::PointingHandCursor);
    btnClearData->setStyleSheet(
        "QPushButton { background: rgba(40,40,55,0.6); "
        "border: 1px solid rgba(180,180,200,0.25); "
        "border-radius: 5px; padding: 2px; } "
        "QPushButton:hover { background: rgba(70,70,90,0.85); "
        "border-color: rgba(220,220,240,0.5); }");
    connect(btnClearData, &QPushButton::clicked, [this]() { m_textData->clear(); });
    dataLayout->addWidget(btnClearData, 0, 0,
                          Qt::AlignTop | Qt::AlignRight);

    dataGroup->setLayout(dataLayout);
    dataGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(dataGroup, 1);

    m_tabWidget->addTab(masterTab, QIcon(":/icons/icons/master.svg"), "Master");
}

void MainWindow::createGatewayTab() {
    QWidget *gatewayTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(gatewayTab);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    QGroupBox *configGroup = new QGroupBox("Gateway Configuration");
    QGridLayout *configGrid = new QGridLayout();
    configGrid->setSpacing(8);
    configGrid->setVerticalSpacing(10);
    configGrid->setColumnMinimumWidth(0, 100);   // primary labels
    configGrid->setColumnStretch(1, 3);           // primary fields
    configGrid->setColumnMinimumWidth(2, 20);     // secondary labels
    configGrid->setColumnStretch(3, 2);           // secondary fields
    configGrid->setColumnMinimumWidth(4, 20);     // tertiary labels
    configGrid->setColumnStretch(5, 1);           // tertiary fields

    // Row 0 — Mode radio buttons
    QHBoxLayout *gwModeLayout = new QHBoxLayout();
    m_radioRtuToTcp = new QRadioButton("RTU to TCP");
    m_radioTcpToRtu = new QRadioButton("TCP to RTU");
    m_radioRtuToTcp->setChecked(true);
    connect(m_radioRtuToTcp, &QRadioButton::toggled, this, &MainWindow::onGatewayModeChanged);
    gwModeLayout->addWidget(m_radioRtuToTcp);
    gwModeLayout->addWidget(m_radioTcpToRtu);
    gwModeLayout->addStretch();
    configGrid->addLayout(gwModeLayout, 0, 0, 1, 6);

    // Row 1 — TCP: Listen IP | Port
    QLabel *lblGwIP = new QLabel("TCP Listen IP:");
    lblGwIP->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configGrid->addWidget(lblGwIP, 1, 0);
    m_editGatewayIP = new QLineEdit("0.0.0.0");
    configGrid->addWidget(m_editGatewayIP, 1, 1);
    QLabel *lblGwPort = new QLabel("Port:");
    lblGwPort->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configGrid->addWidget(lblGwPort, 1, 2);
    m_editGatewayPort = new QLineEdit("502");
    m_editGatewayPort->setMaximumWidth(90);
    configGrid->addWidget(m_editGatewayPort, 1, 3, 1, 3);

    // Row 2 — RTU: COM Port | Baud Rate | Parity
    QLabel *lblGwCOM = new QLabel("COM Port:");
    lblGwCOM->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configGrid->addWidget(lblGwCOM, 2, 0);
    m_comboGatewayCOM = new QComboBox();
    m_comboGatewayCOM->setEditable(true);
#ifdef _WIN32
    m_comboGatewayCOM->addItems({"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8"});
#else
    m_comboGatewayCOM->addItems({"/dev/ttyS0", "/dev/ttyS1", "/dev/ttyUSB0", "/dev/ttyUSB1"});
#endif
    configGrid->addWidget(m_comboGatewayCOM, 2, 1);
    QLabel *lblGwBaud = new QLabel("Baud Rate:");
    lblGwBaud->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configGrid->addWidget(lblGwBaud, 2, 2);
    m_comboGatewayBaud = new QComboBox();
    m_comboGatewayBaud->setEditable(true);
    m_comboGatewayBaud->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_comboGatewayBaud->setCurrentText("9600");
    m_comboGatewayBaud->setValidator(new QIntValidator(300, 4000000, this));
    configGrid->addWidget(m_comboGatewayBaud, 2, 3);
    QLabel *lblGwParity = new QLabel("Parity:");
    lblGwParity->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configGrid->addWidget(lblGwParity, 2, 4);
    m_comboGatewayParity = new QComboBox();
    m_comboGatewayParity->addItems({"None", "Even", "Odd"});
    configGrid->addWidget(m_comboGatewayParity, 2, 5);

    // Row 3 — Buttons
    m_btnStartGateway = new QPushButton(QIcon(":/icons/icons/play.svg"), "Start Gateway");
    m_btnStopGateway = new QPushButton(QIcon(":/icons/icons/stop.svg"), "Stop Gateway");
    m_btnStopGateway->setEnabled(false);
    connect(m_btnStartGateway, &QPushButton::clicked, this, &MainWindow::onStartGateway);
    connect(m_btnStopGateway, &QPushButton::clicked, this, &MainWindow::onStopGateway);
    QHBoxLayout *gwBtnLayout = new QHBoxLayout();
    gwBtnLayout->addWidget(m_btnStartGateway);
    gwBtnLayout->addWidget(m_btnStopGateway);
    gwBtnLayout->addStretch();
    configGrid->addLayout(gwBtnLayout, 3, 0, 1, 6);

    configGroup->setLayout(configGrid);
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

void MainWindow::createScannerTab() {
    QWidget *scannerTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(scannerTab);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Network scanner
    QGroupBox *scanGroup = new QGroupBox("Network Scanner");
    QVBoxLayout *scanLayout = new QVBoxLayout();

    // Row 1: IP range + port + buttons
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
    QLabel *lblScanPort = new QLabel("Port:");
    lblScanPort->setMinimumWidth(35);
    scanControlLayout->addWidget(lblScanPort);
    m_editScanPort = new QLineEdit("502");
    m_editScanPort->setMaximumWidth(65);
    m_editScanPort->setToolTip("TCP port to probe (default 502 for Modbus TCP)");
    scanControlLayout->addWidget(m_editScanPort);
    m_btnScanNetwork = new QPushButton(QIcon(":/icons/icons/search.svg"), "Scan Network");
    m_btnStopScan = new QPushButton(QIcon(":/icons/icons/stop.svg"), "Stop Scan");
    m_btnStopScan->setEnabled(false);
    connect(m_btnScanNetwork, &QPushButton::clicked, this, &MainWindow::onScanNetwork);
    connect(m_btnStopScan, &QPushButton::clicked, this, &MainWindow::onStopScan);
    scanControlLayout->addWidget(m_btnScanNetwork);
    scanControlLayout->addWidget(m_btnStopScan);
    scanControlLayout->addStretch();
    scanLayout->addLayout(scanControlLayout);

    // Row 2: progress bar + status label
    QHBoxLayout *scanProgressLayout = new QHBoxLayout();
    m_scanProgressBar = new QProgressBar();
    m_scanProgressBar->setRange(0, 100);
    m_scanProgressBar->setValue(0);
    m_scanProgressBar->setTextVisible(true);
    m_scanProgressBar->setFixedHeight(18);
    m_scanProgressBar->setVisible(false);
    scanProgressLayout->addWidget(m_scanProgressBar, 1);
    m_scanStatusLabel = new QLabel("Ready");
    m_scanStatusLabel->setMinimumWidth(240);
    scanProgressLayout->addWidget(m_scanStatusLabel);
    scanLayout->addLayout(scanProgressLayout);

    // Row 3: results table
    m_tableScanResults = new QTableWidget();
    m_tableScanResults->setColumnCount(3);
    m_tableScanResults->setHorizontalHeaderLabels({"IP Address", "Port", "Response Time"});
    m_tableScanResults->verticalHeader()->setVisible(false);
    m_tableScanResults->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableScanResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableScanResults->setAlternatingRowColors(true);
    m_tableScanResults->setSortingEnabled(true);
    m_tableScanResults->verticalHeader()->setDefaultSectionSize(32);
    m_tableScanResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableScanResults->setColumnWidth(1, 70);
    m_tableScanResults->setColumnWidth(2, 120);
    m_tableScanResults->setToolTip("Double-click a row to load it into the Master tab");
    connect(m_tableScanResults, &QTableWidget::cellDoubleClicked,
            this, &MainWindow::onScanResultDoubleClicked);
    scanLayout->addWidget(m_tableScanResults);

    scanGroup->setLayout(scanLayout);
    mainLayout->addWidget(scanGroup, 1);

    m_tabWidget->addTab(scannerTab, QIcon(":/icons/icons/scanner.svg"), "Network Scanner");
}

void MainWindow::createLogsTab() {
    QWidget *logsTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(logsTab);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
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
    m_comboLogFilter->addItem("Scanner", LOG_CAT_SCANNER);
    m_comboLogFilter->addItem("System", LOG_CAT_SYSTEM);
    m_comboLogFilter->setMinimumWidth(150);
    connect(m_comboLogFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::onLogFilterChanged);
    filterLayout->addWidget(m_comboLogFilter);
    
    filterLayout->addStretch();
    
    // save.svg isn't bundled — leave the icon off so Qt doesn't warn at runtime
    QPushButton *btnSaveLog = new QPushButton("Save Log");
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
        case LOG_CAT_SCANNER: categoryStr = "[SCANNER]"; break;
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
