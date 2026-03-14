// Slot implementations for MainWindow
#include "mainwindow.h"
#include "devicedialog.h"
#include "alarmdialog.h"
#include "alarm_manager.h"
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "pollinggraphwidget.h"
#endif
#include <cerrno>
#include <cstring>
/* Platform socket headers for gateway */
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET    gw_sock_t;
#  define GW_INVALID  INVALID_SOCKET
#  define gw_close(s) closesocket(s)
#  define gw_error()  WSAGetLastError()
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/select.h>
   typedef int       gw_sock_t;
#  define GW_INVALID  (-1)
#  define gw_close(s) ::close(s)
#  define gw_error()  errno
#endif
#include <QStringList>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QCoreApplication>
#include <QBrush>
#include <QColor>
#include <QHostAddress>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QProcess>

// Master tab slots
void MainWindow::onModeChanged(int index) {
    bool isTCP = (index == 0);
    m_editIP->setEnabled(isTCP);
    m_editPort->setEnabled(isTCP);
    m_comboCOM->setEnabled(!isTCP);
    m_comboBaud->setEnabled(!isTCP);
    m_comboParity->setEnabled(!isTCP);
}

void MainWindow::onConnect() {
    if (m_modbus) {
        modbus_close(m_modbus);
        modbus_free(m_modbus);
        m_modbus = nullptr;
    }
    
    int slaveId = m_editSlave->text().toInt();
    QString logMsg;
    
    if (m_comboMode->currentIndex() == 0) {
        // Modbus TCP
        QString ip = m_editIP->text();
        int port = m_editPort->text().toInt();
        
        m_modbus = modbus_new_tcp(ip.toStdString().c_str(), port);
        modbus_set_slave(m_modbus, slaveId);
        
        if (modbus_connect(m_modbus) == -1) {
            logMsg = QString("[ERROR] TCP connection failed: %1:%2 - %3")
                     .arg(ip).arg(port).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
            updateStatus("Connection failed");
            modbus_free(m_modbus);
            m_modbus = nullptr;
            QMessageBox::warning(this, "Connection Error", "Failed to connect to " + ip);
            return;
        }
        
        logMsg = QString("[CONNECTED] Modbus TCP -> %1:%2 (Slave ID: %3)")
                 .arg(ip).arg(port).arg(slaveId);
        log(LOG_CAT_MASTER, logMsg);
        updateStatus("Connected to Modbus TCP");
        
    } else {
        // Modbus RTU
        QString comPort = m_comboCOM->currentText();
        int baud = m_comboBaud->currentText().toInt();
        QString parityStr = m_comboParity->currentText();
        
        char parity = 'N';
        if (parityStr == "Even") parity = 'E';
        else if (parityStr == "Odd") parity = 'O';
        
        m_modbus = modbus_new_rtu(comPort.toStdString().c_str(), baud, parity, 8, 1);
        modbus_set_slave(m_modbus, slaveId);
        
        if (modbus_connect(m_modbus) == -1) {
            logMsg = QString("[ERROR] RTU connection failed: %1 @ %2 baud - %3")
                     .arg(comPort).arg(baud).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
            updateStatus("Connection failed");
            modbus_free(m_modbus);
            m_modbus = nullptr;
            QMessageBox::warning(this, "Connection Error", "Failed to connect to " + comPort);
            return;
        }
        
        logMsg = QString("[CONNECTED] Modbus RTU -> %1 @ %2 baud (Slave ID: %3)")
                 .arg(comPort).arg(baud).arg(slaveId);
        log(LOG_CAT_MASTER, logMsg);
        updateStatus("Connected to Modbus RTU");
    }
    
    m_connected = true;
    enableConnectionControls(true);
    APP_LOG_INFO("Connection", logMsg.toStdString().c_str());
}

void MainWindow::onDisconnect() {
    if (m_modbus) {
        modbus_close(m_modbus);
        modbus_free(m_modbus);
        m_modbus = nullptr;
    }
    
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
        m_checkPoll->setChecked(false);
    }
    
    log(LOG_CAT_MASTER, "[DISCONNECTED] Connection closed");
    updateStatus("Disconnected");
    m_connected = false;
    enableConnectionControls(false);
    APP_LOG_INFO("Connection", "Disconnected");
}

void MainWindow::onRead() {
    if (!m_modbus || !m_connected) return;
    
    int funcIndex = m_comboFunction->currentIndex();
    int address = m_editAddress->text().toInt();
    int quantity = m_editQuantity->text().toInt();
    QString logMsg;
    int rc = -1;
    
    // Clear data display
    m_textData->clear();
    
    if (funcIndex == 0 || funcIndex == 1) {
        // Read coils or discrete inputs
        uint8_t *tabBits = new uint8_t[quantity];
        
        if (funcIndex == 0) {
            rc = modbus_read_bits(m_modbus, address, quantity, tabBits);
        } else {
            rc = modbus_read_input_bits(m_modbus, address, quantity, tabBits);
        }
        
        if (rc == quantity) {
            int fc = (funcIndex == 0) ? 1 : 2;
            logMsg = QString("[READ] FC=0x%1: Read %2 items from address %3")
                     .arg(fc, 2, 16, QChar('0')).arg(quantity).arg(address);
            log(LOG_CAT_MASTER, logMsg);
            
            int baseAddr = (funcIndex == 0) ? 1 : 10001;
            for (int i = 0; i < quantity; i++) {
                QString line = QString("Address [%1]: %2")
                              .arg(baseAddr + address + i, 5, 10, QChar('0'))
                              .arg(tabBits[i]);
                addLogMessage(m_textData, line);
            }
        } else {
            int fc = (funcIndex == 0) ? 1 : 2;
            logMsg = QString("[ERROR] Read failed: FC=0x%1, Addr=%2, Qty=%3 - %4")
                     .arg(fc, 2, 16, QChar('0')).arg(address).arg(quantity)
                     .arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
        delete[] tabBits;
        
    } else if (funcIndex <= 3) {
        // Read registers
        uint16_t *tabRegs = new uint16_t[quantity];
        
        if (funcIndex == 2) {
            rc = modbus_read_registers(m_modbus, address, quantity, tabRegs);
        } else {
            rc = modbus_read_input_registers(m_modbus, address, quantity, tabRegs);
        }
        
        if (rc == quantity) {
            int fc = (funcIndex == 2) ? 3 : 4;
            logMsg = QString("[READ] FC=0x%1: Read %2 items from address %3")
                     .arg(fc, 2, 16, QChar('0')).arg(quantity).arg(address);
            log(LOG_CAT_MASTER, logMsg);
            
            int baseAddr = (funcIndex == 2) ? 40001 : 30001;
            for (int i = 0; i < quantity; i++) {
                QString line;
                if (m_displayFormat == 1) { // Hex
                    line = QString("Address [%1]: 0x%2")
                          .arg(baseAddr + address + i, 5, 10, QChar('0'))
                          .arg(tabRegs[i], 4, 16, QChar('0')).toUpper();
                } else if (m_displayFormat == 2) { // Octal
                    line = QString("Address [%1]: %2")
                          .arg(baseAddr + address + i, 5, 10, QChar('0'))
                          .arg(tabRegs[i], 0, 8);
                } else { // Decimal
                    line = QString("Address [%1]: %2")
                          .arg(baseAddr + address + i, 5, 10, QChar('0'))
                          .arg(tabRegs[i]);
                }
                addLogMessage(m_textData, line);
            }
        } else {
            int fc = (funcIndex == 2) ? 3 : 4;
            logMsg = QString("[ERROR] Read failed: FC=0x%1, Addr=%2, Qty=%3 - %4")
                     .arg(fc, 2, 16, QChar('0')).arg(address).arg(quantity)
                     .arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
        delete[] tabRegs;
    }
}

void MainWindow::onWrite() {
    if (!m_modbus || !m_connected) return;
    
    int funcIndex = m_comboFunction->currentIndex();
    int address = m_editAddress->text().toInt();
    QString valueStr = m_editValue->text();
    QString logMsg;
    int rc = -1;
    
    if (funcIndex == 4) {
        // Write single coil (FC 0x05)
        int value = valueStr.toInt();
        rc = modbus_write_bit(m_modbus, address, value ? 1 : 0);
        
        if (rc == 1) {
            logMsg = QString("[WRITE] FC=0x05: Wrote %1 to address %2")
                     .arg(value).arg(address);
            log(LOG_CAT_MASTER, logMsg);
        } else {
            logMsg = QString("[ERROR] Write failed: FC=0x05, Addr=%1 - %2")
                     .arg(address).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
    } else if (funcIndex == 5) {
        // Write single register (FC 0x06)
        int value = valueStr.toInt();
        rc = modbus_write_register(m_modbus, address, value);
        
        if (rc == 1) {
            logMsg = QString("[WRITE] FC=0x06: Wrote %1 to address %2")
                     .arg(value).arg(address);
            log(LOG_CAT_MASTER, logMsg);
        } else {
            logMsg = QString("[ERROR] Write failed: FC=0x06, Addr=%1 - %2")
                     .arg(address).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
    } else if (funcIndex == 6) {
        // Write multiple coils (FC 0x0F)
        QStringList values = valueStr.split(',');
        int quantity = values.size();
        uint8_t *tabBits = new uint8_t[quantity];
        
        for (int i = 0; i < quantity; i++) {
            tabBits[i] = values[i].trimmed().toInt() ? 1 : 0;
        }
        
        rc = modbus_write_bits(m_modbus, address, quantity, tabBits);
        
        if (rc == quantity) {
            logMsg = QString("[WRITE] FC=0x0F: Wrote %1 coils starting at address %2")
                     .arg(quantity).arg(address);
            log(LOG_CAT_MASTER, logMsg);
        } else {
            logMsg = QString("[ERROR] Write failed: FC=0x0F, Addr=%1, Qty=%2 - %3")
                     .arg(address).arg(quantity).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
        delete[] tabBits;
        
    } else if (funcIndex == 7) {
        // Write multiple registers (FC 0x10)
        QStringList values = valueStr.split(',');
        int quantity = values.size();
        uint16_t *tabRegs = new uint16_t[quantity];
        
        for (int i = 0; i < quantity; i++) {
            tabRegs[i] = values[i].trimmed().toInt();
        }
        
        rc = modbus_write_registers(m_modbus, address, quantity, tabRegs);
        
        if (rc == quantity) {
            logMsg = QString("[WRITE] FC=0x10: Wrote %1 registers starting at address %2")
                     .arg(quantity).arg(address);
            log(LOG_CAT_MASTER, logMsg);
        } else {
            logMsg = QString("[ERROR] Write failed: FC=0x10, Addr=%1, Qty=%2 - %3")
                     .arg(address).arg(quantity).arg(modbus_strerror(errno));
            log(LOG_CAT_MASTER, logMsg);
        }
        
        delete[] tabRegs;
    }
}

void MainWindow::onClearData() {
    m_textData->clear();
}

void MainWindow::onPollToggled(bool checked) {
    if (checked && m_connected) {
        int interval = m_editPollInterval->text().toInt();
        if (interval < 100) interval = 100; // Minimum 100ms
        m_pollTimer->start(interval);
        m_polling = true;
        log(LOG_CAT_MASTER, QString("[POLLING] Started auto-polling every %1 ms").arg(interval));
    } else {
        m_pollTimer->stop();
        m_polling = false;
        log(LOG_CAT_MASTER, "[POLLING] Stopped auto-polling");
    }
}

void MainWindow::onPollTimer() {
    onRead();
}

void MainWindow::onFunctionChanged(int index) {
    // Enable/disable write controls based on function
    bool isWrite = (index >= 4);
    m_editValue->setEnabled(isWrite);
    m_btnWrite->setEnabled(m_connected && isWrite);
    m_btnRead->setEnabled(m_connected && !isWrite);
    
    // Adjust UI for multi-value writes
    if (index == 6 || index == 7) {
        m_editValue->setPlaceholderText("Comma-separated values: 1,2,3,4");
    } else {
        m_editValue->setPlaceholderText("Single value");
    }
}

void MainWindow::onFormatChanged(int index) {
    m_displayFormat = index;
    // Re-read to update display format
    if (m_connected && !m_polling) {
        int funcIndex = m_comboFunction->currentIndex();
        if (funcIndex <= 3) { // Only for read operations
            onRead();
        }
    }
}

// Gateway tab slots
void MainWindow::onGatewayModeChanged(bool checked) {
    m_gatewayIsRtuToTcp = m_radioRtuToTcp->isChecked();
}

void MainWindow::onStartGateway() {
    QString tcpIP = m_editGatewayIP->text();
    int tcpPort = m_editGatewayPort->text().toInt();
    QString comPort = m_comboGatewayCOM->currentText();
    int baudRate = m_comboGatewayBaud->currentText().toInt();
    QString parityStr = m_comboGatewayParity->currentText();
    
    char parity = 'N';
    if (parityStr == "Even") parity = 'E';
    else if (parityStr == "Odd") parity = 'O';
    
    m_gatewayThread = new GatewayThread(this);
    m_gatewayThread->setParameters(m_gatewayIsRtuToTcp, tcpIP, tcpPort, comPort, baudRate, parity);
    
    connect(m_gatewayThread, &GatewayThread::logMessage,
            [this](const QString &msg) { log(LOG_CAT_GATEWAY, msg); });
    
    m_gatewayThread->start();
    
    m_gatewayRunning = true;
    m_btnStartGateway->setEnabled(false);
    m_btnStopGateway->setEnabled(true);
    
    QString logMsg = QString("[GATEWAY] Started %1 mode on %2:%3 <-> %4 @ %5 baud")
                     .arg(m_gatewayIsRtuToTcp ? "RTU-to-TCP" : "TCP-to-RTU")
                     .arg(tcpIP).arg(tcpPort).arg(comPort).arg(baudRate);
    log(LOG_CAT_GATEWAY, logMsg);
    updateStatus("Gateway running");
}

void MainWindow::onStopGateway() {
    if (m_gatewayThread) {
        m_gatewayThread->stop();
        m_gatewayThread->wait();
        delete m_gatewayThread;
        m_gatewayThread = nullptr;
    }
    
    m_gatewayRunning = false;
    m_btnStartGateway->setEnabled(true);
    m_btnStopGateway->setEnabled(false);
    
    log(LOG_CAT_GATEWAY, "[GATEWAY] Stopped");
    updateStatus("Gateway stopped");
}

// Device tab slots
void MainWindow::onAddDevice() {
    DeviceDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        modbus_device_t device = dialog.getDeviceConfig();
        int id = device_manager_add(&device);
        if (id >= 0) {
            refreshDeviceList();
            log(LOG_CAT_DEVICES, QString("[DEVICES] Added device: %1").arg(device.name));
            // Poll the register address the user specified
            modbus_device_t* dev = device_manager_get(id);
            if (dev && device_manager_connect(id) == 0 && dev->ctx) {
                uint16_t regValue = 0;
                int pollRegister = static_cast<int>(dev->poll_register);
                int rc = modbus_read_registers((modbus_t*)dev->ctx, pollRegister, 1, &regValue);
                if (rc == 1) {
                    log(LOG_CAT_DEVICES, QString("[POLL] Device %1 Register %2 = %3").arg(dev->name).arg(pollRegister).arg(regValue));
                } else {
                    log(LOG_CAT_DEVICES, QString("[POLL] Device %1 Register %2 read failed").arg(dev->name).arg(pollRegister));
                }
            }
            // Persist: save commissioned device to backup immediately
            device_manager_save("devices.ini");
            log(LOG_CAT_SYSTEM, QString("[BACKUP] Device '%1' saved to devices.ini").arg(device.name));
        } else {
            QMessageBox::warning(this, "Add Device", "Failed to add device. Maximum devices reached?");
        }
    }
}

void MainWindow::onRemoveDevice() {
    int row = m_tableDevices->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Remove Device", "Please select a device to remove.");
        return;
    }
    
    int deviceId = m_tableDevices->item(row, 0)->text().toInt();
    device_manager_remove(deviceId);
    refreshDeviceList();
    // Persist: update backup after removal
    device_manager_save("devices.ini");
    log(LOG_CAT_SYSTEM, "[BACKUP] Device list saved to devices.ini");
}

void MainWindow::onEditDevice() {
    int row = m_tableDevices->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Edit Device", "Please select a device to edit.");
        return;
    }
    
    int deviceId = m_tableDevices->item(row, 0)->text().toInt();
    modbus_device_t* device = device_manager_get(deviceId);
    if (!device) {
        QMessageBox::warning(this, "Edit Device", "Device not found.");
        return;
    }
    
    DeviceDialog dialog(this, device);
    if (dialog.exec() == QDialog::Accepted) {
        modbus_device_t updatedDevice = dialog.getDeviceConfig();
        if (device_manager_update(deviceId, &updatedDevice) == 0) {
            refreshDeviceList();
            log(LOG_CAT_DEVICES, QString("[DEVICES] Updated device: %1").arg(updatedDevice.name));
            // Persist: save updated device configuration
            device_manager_save("devices.ini");
            log(LOG_CAT_SYSTEM, QString("[BACKUP] Device '%1' updated in devices.ini").arg(updatedDevice.name));
        } else {
            QMessageBox::warning(this, "Edit Device", "Failed to update device.");
        }
    }
}

void MainWindow::onDeviceSelected(int row, int column) {
    Q_UNUSED(column);
    bool hasSelection = (row >= 0 && row < device_manager_get_count());
    m_btnRemoveDevice->setEnabled(hasSelection);
    m_btnEditDevice->setEnabled(hasSelection);
}

void MainWindow::onConnectAll() {
    if (!m_deviceWorker) {
        m_deviceWorker = new DeviceWorkerThread(this);
        connect(m_deviceWorker, &DeviceWorkerThread::deviceLog,
                this, &MainWindow::onDeviceWorkerLog, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::pollResult,
                this, &MainWindow::onDeviceWorkerPollResult, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::requestRefresh,
                this, &MainWindow::onDeviceWorkerRefresh, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::deviceDisconnected,
                this, &MainWindow::onDeviceWorkerDisconnected, Qt::QueuedConnection);
    }
    m_deviceWorker->requestConnectAll();
    log(LOG_CAT_DEVICES, "[DEVICES] Connecting to all enabled devices...");
}

void MainWindow::onDisconnectAll() {
    if (m_deviceWorker) {
        m_deviceWorker->requestDisconnectAll();
    } else {
        device_manager_disconnect_all();
    }
    log(LOG_CAT_DEVICES, "[DEVICES] Disconnecting all devices...");
}

void MainWindow::onMonitorAll() {
    if (!m_deviceWorker) {
        m_deviceWorker = new DeviceWorkerThread(this);
        connect(m_deviceWorker, &DeviceWorkerThread::deviceLog,
                this, &MainWindow::onDeviceWorkerLog, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::pollResult,
                this, &MainWindow::onDeviceWorkerPollResult, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::requestRefresh,
                this, &MainWindow::onDeviceWorkerRefresh, Qt::QueuedConnection);
        connect(m_deviceWorker, &DeviceWorkerThread::deviceDisconnected,
                this, &MainWindow::onDeviceWorkerDisconnected, Qt::QueuedConnection);
    }
    m_monitoringAll = true;
    m_deviceWorker->startMonitoring(1000);
    m_btnMonitorAll->setEnabled(false);
    m_btnStopMonitor->setEnabled(true);
    log(LOG_CAT_DEVICES, "[MONITOR] Started monitoring all devices");
}

void MainWindow::onStopMonitor() {
    m_monitoringAll = false;
    if (m_deviceWorker) {
        m_deviceWorker->stopMonitoring();
        m_deviceWorker->wait();
    }
    m_btnMonitorAll->setEnabled(true);
    m_btnStopMonitor->setEnabled(false);
    log(LOG_CAT_DEVICES, "[MONITOR] Stopped monitoring");
}

void MainWindow::onScanNetwork() {
    // Parse IP range
    QString startIP = m_editScanStartIP->text();
    QString endIP = m_editScanEndIP->text();
    
    QHostAddress startAddr(startIP);
    QHostAddress endAddr(endIP);
    
    if (startAddr.isNull() || endAddr.isNull()) {
        QMessageBox::warning(this, "Network Scanner", "Invalid IP address format.");
        return;
    }
    
    m_scanCurrentIP = startAddr.toIPv4Address();
    m_scanEndIP = endAddr.toIPv4Address();
    m_scanPort = 502; // Modbus TCP default port
    
    if (m_scanCurrentIP > m_scanEndIP) {
        QMessageBox::warning(this, "Network Scanner", "Start IP must be less than End IP.");
        return;
    }
    
    // Clear previous results
    m_listScanResults->clear();
    
    // Update UI state
    m_scanningNetwork = true;
    m_btnScanNetwork->setEnabled(false);
    m_btnStopScan->setEnabled(true);
    
    log(LOG_CAT_DEVICES, QString("[SCAN] Starting network scan from %1 to %2").arg(startIP).arg(endIP));
    
    // Start scanning (scan every 50ms for responsiveness)
    m_scanTimer->start(50);
}

void MainWindow::onStopScan() {
    m_scanningNetwork = false;
    m_scanTimer->stop();
    m_btnScanNetwork->setEnabled(true);
    m_btnStopScan->setEnabled(false);
    log(LOG_CAT_DEVICES, "[SCAN] Network scan stopped by user");
}

void MainWindow::onScanTimer() {
    if (!m_scanningNetwork || m_scanCurrentIP > m_scanEndIP) {
        // Scan complete
        m_scanTimer->stop();
        m_scanningNetwork = false;
        m_btnScanNetwork->setEnabled(true);
        m_btnStopScan->setEnabled(false);
        log(LOG_CAT_DEVICES, QString("[SCAN] Network scan complete. Found %1 device(s)").arg(m_listScanResults->count()));
        return;
    }
    
    // Convert current IP to string
    QHostAddress addr(m_scanCurrentIP);
    QString ipStr = addr.toString();
    
    // Try to connect to Modbus TCP port with short timeout
    QTcpSocket socket;
    socket.connectToHost(addr, m_scanPort);
    
    // Wait for connection with 100ms timeout
    if (socket.waitForConnected(100)) {
        // Found a device responding on Modbus TCP port
        QString result = QString("%1:%2 - Modbus TCP device found").arg(ipStr).arg(m_scanPort);
        m_listScanResults->addItem(result);
        log(LOG_CAT_DEVICES, QString("[SCAN] Found device at %1").arg(ipStr));
        socket.disconnectFromHost();
    }
    
    // Move to next IP
    m_scanCurrentIP++;
    
    // Update status periodically (every 10 IPs)
    if ((m_scanCurrentIP - QHostAddress(m_editScanStartIP->text()).toIPv4Address()) % 10 == 0) {
        updateStatus(QString("Scanning: %1").arg(ipStr));
    }
}

// Dashboard tab slots
void MainWindow::onConfigureAlarms() {
    AlarmDialog dialog(this);
    dialog.loadAlarmsFromManager();
    if (dialog.exec() == QDialog::Accepted) {
        dialog.saveAlarmsToManager();
        int count = alarm_manager_get_count();
        log(LOG_CAT_SYSTEM, QString("[ALARMS] Configured %1 alarm(s)").arg(count));
    }
}

void MainWindow::onRefreshDashboard() {
    updateDashboardMetrics();
    log(LOG_CAT_SYSTEM, "[DASHBOARD] Metrics refreshed");
}

void MainWindow::onDarkModeToggled(bool checked) {
    // Try to load stylesheet from file, with fallback to embedded styles
    QStringList basePaths = {
        QDir::currentPath() + "/src/ui/resources/",
        QDir::currentPath() + "/",
        QCoreApplication::applicationDirPath() + "/"
    };
    
    QString targetFile = checked ? "macos_style_dark.qss" : "macos_style.qss";
    bool styleLoaded = false;
    
    for (const QString &basePath : basePaths) {
        QFile styleFile(basePath + targetFile);
        if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
            QString stylesheet = QString::fromUtf8(styleFile.readAll());
            qApp->setStyleSheet(stylesheet);
            styleFile.close();
            styleLoaded = true;
            break;
        }
    }
    
    if (!styleLoaded) {
        // Fallback to embedded minimal styles
        if (checked) {
            qApp->setStyleSheet(R"(
                QWidget { font-family: "Segoe UI", Arial; font-size: 13px; background-color: #1c1c1e; color: #f5f5f7; }
                QGroupBox { border: 1px solid #3a3a3c; border-radius: 12px; margin-top: 16px; padding-top: 16px; background-color: #2c2c2e; }
                QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 4px 12px; color: #98989d; text-transform: uppercase; }
                QPushButton { background-color: #3a3a3c; border: 1px solid #48484a; border-radius: 8px; padding: 8px 16px; color: #f5f5f7; }
                QPushButton:hover { background-color: #48484a; }
                QLineEdit, QComboBox { background-color: #1c1c1e; border: 1px solid #3a3a3c; border-radius: 8px; padding: 8px 12px; color: #f5f5f7; }
                QLineEdit:focus, QComboBox:focus { border-color: #0a84ff; }
                QTextEdit { background-color: #1c1c1e; border: 1px solid #3a3a3c; border-radius: 10px; padding: 12px; color: #f5f5f7; }
                QTableWidget { background-color: #2c2c2e; border: 1px solid #3a3a3c; border-radius: 10px; gridline-color: #3a3a3c; }
                QTabWidget::pane { border: 1px solid #3a3a3c; border-radius: 10px; background-color: #2c2c2e; }
                QTabBar::tab { padding: 8px 20px; border-radius: 6px; color: #98989d; }
                QTabBar::tab:selected { background-color: #3a3a3c; color: #f5f5f7; }
                QStatusBar { background-color: #1c1c1e; border-top: 1px solid #3a3a3c; }
            )");
        } else {
            qApp->setStyleSheet(R"(
                QWidget { font-family: "Segoe UI", Arial; font-size: 13px; color: #1d1d1f; }
                QMainWindow { background-color: #f5f5f7; }
                QGroupBox { border: 1px solid #e5e5e7; border-radius: 12px; margin-top: 16px; padding-top: 16px; background-color: #ffffff; }
                QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 4px 12px; color: #6e6e73; text-transform: uppercase; }
                QPushButton { background-color: #ffffff; border: 1px solid #d2d2d7; border-radius: 8px; padding: 8px 16px; }
                QPushButton:hover { background-color: #f5f5f7; }
                QLineEdit, QComboBox { background-color: #ffffff; border: 1px solid #d2d2d7; border-radius: 8px; padding: 8px 12px; }
                QLineEdit:focus, QComboBox:focus { border-color: #007aff; }
                QTextEdit { background-color: #ffffff; border: 1px solid #d2d2d7; border-radius: 10px; padding: 12px; }
                QTableWidget { background-color: #ffffff; border: 1px solid #d2d2d7; border-radius: 10px; gridline-color: #e5e5e7; }
                QTabWidget::pane { border: 1px solid #d2d2d7; border-radius: 10px; background-color: #ffffff; }
                QTabBar::tab { padding: 8px 20px; border-radius: 6px; color: #6e6e73; }
                QTabBar::tab:selected { background-color: #ffffff; color: #1d1d1f; border: 1px solid #d2d2d7; }
                QStatusBar { background-color: #f5f5f7; border-top: 1px solid #d2d2d7; }
            )");
        }
    }
    
    log(LOG_CAT_SYSTEM, checked ? "[UI] Dark mode enabled" : "[UI] Light mode enabled");
}

void MainWindow::onOpenManual() {
    // Write the manual HTML from Qt resources to a temp file, then open in browser
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                       + "/modbus_hub_manual.html";

    QFile src(":/docs/manual.html");
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "User Manual",
            "Could not locate the manual resource.\n"
            "Please rebuild the application to ensure manual.html is included.");
        return;
    }
    QByteArray content = src.readAll();
    src.close();

    QFile dst(tempPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "User Manual",
            "Could not write manual to temporary directory:\n" + tempPath);
        return;
    }
    dst.write(content);
    dst.close();

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath))) {
#if defined(Q_OS_LINUX)
        // Fallback: try xdg-open directly
        if (!QProcess::startDetached("xdg-open", QStringList() << tempPath)) {
            QMessageBox::warning(this, "User Manual",
                "Could not open the manual in a web browser.\n"
                "Manual is located at:\n" + tempPath);
        }
#elif defined(Q_OS_WIN)
        // Fallback: use explorer
        if (!QProcess::startDetached("explorer", QStringList() << tempPath)) {
            QMessageBox::warning(this, "User Manual",
                "Could not open the manual in a web browser.\n"
                "Manual is located at:\n" + tempPath);
        }
#else
        QMessageBox::warning(this, "User Manual",
            "Could not open the manual in a web browser.\n"
            "Manual is located at:\n" + tempPath);
#endif
    }
    log(LOG_CAT_SYSTEM, "[SETTINGS] User manual opened in browser");
}

// Helper methods - used only for m_textData (register values display)
void MainWindow::addLogMessage(QTextEdit *textEdit, const QString &message) {
    if (!textEdit) return;
    
    // Limit to 500 lines
    QTextDocument *doc = textEdit->document();
    if (doc->lineCount() >= 500) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::LineUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar(); // Remove the newline
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    textEdit->append(QString("[%1] %2").arg(timestamp).arg(message));
}

void MainWindow::updateStatus(const QString &message) {
    m_statusLabel->setText(message);
}

void MainWindow::enableConnectionControls(bool connected) {
    m_btnConnect->setEnabled(!connected);
    m_btnDisconnect->setEnabled(connected);
    m_btnRead->setEnabled(connected && m_comboFunction->currentIndex() <= 3);
    m_btnWrite->setEnabled(connected && m_comboFunction->currentIndex() >= 4);
}

void MainWindow::refreshDeviceList() {
    m_tableDevices->setRowCount(0);
    
    int deviceCount = device_manager_get_count();
    
    for (int i = 0; i < deviceCount; i++) {
        modbus_device_t* device = device_manager_get_by_index(i);
        if (device) {
            int row = m_tableDevices->rowCount();
            m_tableDevices->insertRow(row);
            
            m_tableDevices->setItem(row, 0, new QTableWidgetItem(QString::number(device->id)));
            m_tableDevices->setItem(row, 1, new QTableWidgetItem(device->name));
            m_tableDevices->setItem(row, 2, new QTableWidgetItem(
                device->type == DEVICE_TYPE_TCP ? "TCP" : "RTU"));
            
            QString connInfo;
            if (device->type == DEVICE_TYPE_TCP) {
                connInfo = QString("%1:%2").arg(device->ip_address).arg(device->port);
            } else {
                connInfo = QString("%1 @ %2").arg(device->com_port).arg(device->baud_rate);
            }
            m_tableDevices->setItem(row, 3, new QTableWidgetItem(connInfo));
            m_tableDevices->setItem(row, 4, new QTableWidgetItem(QString::number(device->slave_id)));
            m_tableDevices->setItem(row, 5, new QTableWidgetItem(QString::number(device->poll_interval_ms)));
            m_tableDevices->setItem(row, 6, new QTableWidgetItem(device->enabled ? "Yes" : "No"));
            
            // Status column with color indicator
            QTableWidgetItem* statusItem = new QTableWidgetItem(device->connected ? "Connected" : "Disconnected");
            if (device->connected) {
                statusItem->setForeground(QBrush(QColor(52, 199, 89)));  // Green
            } else {
                statusItem->setForeground(QBrush(QColor(255, 59, 48))); // Red
            }
            m_tableDevices->setItem(row, 7, statusItem);
        }
    }
}

void MainWindow::updateDashboardMetrics() {
    int deviceCount = device_manager_get_count();
    int connectedCount = 0;
    int enabledCount = 0;
    uint32_t totalReads = 0;
    uint32_t totalWrites = 0;
    uint32_t totalErrors = 0;
    
    for (int i = 0; i < deviceCount; i++) {
        modbus_device_t* device = device_manager_get_by_index(i);
        if (device) {
            if (device->connected) connectedCount++;
            if (device->enabled) enabledCount++;
            totalReads += device->successful_reads;
            totalWrites += device->successful_writes;
            totalErrors += device->failed_reads + device->failed_writes;
        }
    }
    
    m_metricLabels[0]->setText(QString("Devices: %1 (%2 enabled)").arg(deviceCount).arg(enabledCount));
    m_metricLabels[1]->setText(QString("Connected: %1 / %2").arg(connectedCount).arg(enabledCount));
    m_metricLabels[2]->setText(QString("Master: %1").arg(m_connected ? "Connected" : "Disconnected"));
    m_metricLabels[3]->setText(QString("Gateway: %1").arg(m_gatewayRunning ? "Running" : "Stopped"));
    m_metricLabels[4]->setText(QString("Total R/W: %1 / %2").arg(totalReads).arg(totalWrites));
    m_metricLabels[5]->setText(QString("Errors: %1").arg(totalErrors));
}

/* ══════════════════════════════════════════════════════════════════════
   DeviceWorkerThread — all device I/O off the UI thread
   ══════════════════════════════════════════════════════════════════════ */

DeviceWorkerThread::DeviceWorkerThread(QObject *parent)
    : QThread(parent), m_shouldStop(0), m_command(CMD_NONE), m_intervalMs(1000)
{
}

DeviceWorkerThread::~DeviceWorkerThread() {
    stopMonitoring();
    wait();
}

void DeviceWorkerThread::startMonitoring(int intervalMs) {
    m_intervalMs = intervalMs;
    m_shouldStop.storeRelaxed(0);
    m_command.storeRelaxed(CMD_NONE);
    if (!isRunning()) start();
}

void DeviceWorkerThread::stopMonitoring() {
    m_shouldStop.storeRelaxed(1);
}

void DeviceWorkerThread::requestConnectAll() {
    m_command.storeRelaxed(CMD_CONNECT_ALL);
    /* If the thread isn't running yet (monitoring not started),
       start it briefly to execute the one-shot command. */
    if (!isRunning()) {
        m_shouldStop.storeRelaxed(0);
        start();
    }
}

void DeviceWorkerThread::requestDisconnectAll() {
    m_command.storeRelaxed(CMD_DISCONNECT_ALL);
    if (!isRunning()) {
        m_shouldStop.storeRelaxed(0);
        start();
    }
}

void DeviceWorkerThread::run() {
    while (!m_shouldStop.loadRelaxed()) {
        /* ── Handle one-shot commands (connect / disconnect) ── */
        int cmd = m_command.fetchAndStoreRelaxed(CMD_NONE);
        if (cmd == CMD_CONNECT_ALL) {
            emit deviceLog("[DEVICES] Connecting to all enabled devices...", LOG_CAT_DEVICES);
            device_manager_connect_all();
            emit requestRefresh();
        } else if (cmd == CMD_DISCONNECT_ALL) {
            device_manager_disconnect_all();
            emit deviceLog("[DEVICES] Disconnected from all devices", LOG_CAT_DEVICES);
            emit requestRefresh();
        }

        /* ── Poll all devices ── */
        int deviceCount = device_manager_get_count();
        bool needRefresh = false;

        for (int i = 0; i < deviceCount && !m_shouldStop.loadRelaxed(); i++) {
            modbus_device_t* device = device_manager_get_by_index(i);
            if (!device || !device->enabled) continue;

            /* Auto-reconnect disconnected devices */
            if (!device->connected || !device->ctx) {
                if (device_manager_connect(device->id) == 0)
                    needRefresh = true;
                continue;
            }

            /* Poll */
            uint16_t regValue = 0;
            int pollReg = static_cast<int>(device->poll_register);
            int rc = modbus_read_registers((modbus_t*)device->ctx, pollReg, 1, &regValue);

            if (rc == 1) {
                device->consecutive_failures = 0;
                device->successful_reads++;
                emit pollResult(device->id, QString::fromUtf8(device->name),
                                pollReg, static_cast<int>(regValue));

                alarm_manager_check_value(device->id, pollReg, (int)regValue);
            } else {
                device->consecutive_failures++;
                device->failed_reads++;
                emit deviceLog(
                    QString("[DEVICE %1] %2: Register[%3] read failed - %4")
                        .arg(device->id).arg(device->name).arg(pollReg)
                        .arg(modbus_strerror(errno)),
                    LOG_CAT_DEVICES);

                if (device->consecutive_failures >= 3) {
                    emit deviceLog(
                        QString("[DEVICE %1] %2: 3 consecutive failures — marking disconnected")
                            .arg(device->id).arg(device->name),
                        LOG_CAT_DEVICES);
                    emit deviceDisconnected(device->id, QString::fromUtf8(device->name));
                    device_manager_disconnect(device->id);
                    device->consecutive_failures = 0;
                    needRefresh = true;
                }
            }
        }

        if (needRefresh) emit requestRefresh();

        /* Sleep in small chunks so stop is responsive */
        for (int waited = 0; waited < m_intervalMs && !m_shouldStop.loadRelaxed(); waited += 50)
            msleep(50);
    }

    /* If we were asked to disconnect before exiting, honour it */
    int cmd = m_command.fetchAndStoreRelaxed(CMD_NONE);
    if (cmd == CMD_DISCONNECT_ALL) {
        device_manager_disconnect_all();
        emit requestRefresh();
    }
}

/* ── Slots in MainWindow receiving worker signals ── */

void MainWindow::onDeviceWorkerLog(const QString &msg, int category) {
    log(static_cast<LogCategory>(category), msg);
}

void MainWindow::onDeviceWorkerPollResult(int deviceId, const QString &deviceName,
                                           int pollReg, int value) {
    QString logMsg = QString("[DEVICE %1] %2: Register[%3] = %4")
                     .arg(deviceId).arg(deviceName).arg(pollReg).arg(value);
    log(LOG_CAT_DEVICES, logMsg);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (m_pollingGraphWidget) {
        m_pollingGraphWidget->addDataPoint(
            deviceId,
            QString("%1 [R%2]").arg(deviceName).arg(pollReg),
            static_cast<qreal>(value));
    }
#endif
}

void MainWindow::onDeviceWorkerRefresh() {
    refreshDeviceList();
}

void MainWindow::onDeviceWorkerDisconnected(int deviceId, const QString &deviceName) {
    Q_UNUSED(deviceId); Q_UNUSED(deviceName);
    refreshDeviceList();
}

// GatewayThread implementation
GatewayThread::GatewayThread(MainWindow *parent)
    : QThread(parent)
    , m_mainWindow(parent)
    , m_shouldStop(false)
    , m_gatewayTcp(nullptr)
    , m_gatewayRtu(nullptr)
    , m_serverSock(-1)
{
}

GatewayThread::~GatewayThread() {
    stop();
    wait();
}

void GatewayThread::setParameters(bool isRtuToTcp, const QString &tcpIP, int tcpPort,
                                   const QString &comPort, int baudRate, char parity) {
    m_isRtuToTcp = isRtuToTcp;
    m_tcpIP = tcpIP;
    m_tcpPort = tcpPort;
    m_comPort = comPort;
    m_baudRate = baudRate;
    m_parity = parity;
}

void GatewayThread::stop() {
    m_shouldStop = true;
}

void GatewayThread::run() {
    emit logMessage("[GATEWAY] Thread started");

#ifdef _WIN32
    /* Winsock must be initialised before any socket() / bind() / etc.
       In RTU-to-TCP mode modbus_new_tcp() is never called, so we do it here. */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        emit logMessage("[GATEWAY ERROR] WSAStartup failed");
        return;
    }
#endif

    /* CRC-16/IBM for RTU framing */
    auto crc16 = [](const uint8_t* b, int n) -> uint16_t {
        uint16_t crc = 0xFFFF;
        for (int i = 0; i < n; i++) {
            crc ^= b[i];
            for (int j = 0; j < 8; j++)
                crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
        return crc;
    };

    /* Receive exactly `need` bytes from a TCP socket within timeout_ms */
    auto tcp_recv_all = [](gw_sock_t s, uint8_t* buf, int need, int timeout_ms) -> int {
        int total = 0;
        while (total < need) {
            fd_set fds; struct timeval tv;
            FD_ZERO(&fds); FD_SET(s, &fds);
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            if (select((int)s + 1, &fds, NULL, NULL, &tv) <= 0) break;
            int rd = (int)recv(s, (char*)buf + total, need - total, 0);
            if (rd <= 0) break;
            total += rd;
        }
        return total;
    };

    if (m_isRtuToTcp) {
        /* ════ RTU-to-TCP: listen on TCP, bridge to serial ════ */

        /* 1. Open serial port */
        m_gatewayRtu = modbus_new_rtu(m_comPort.toStdString().c_str(),
                                      m_baudRate, m_parity, 8, 1);
        if (!m_gatewayRtu || modbus_connect(m_gatewayRtu) != 0) {
            emit logMessage(QString("[GATEWAY ERROR] Cannot open serial port %1").arg(m_comPort));
            goto cleanup;
        }

        /* 2. Create TCP listen socket */
        {
            gw_sock_t listenSock = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listenSock == GW_INVALID) {
                emit logMessage("[GATEWAY ERROR] Failed to create TCP listen socket");
                goto cleanup;
            }
            m_serverSock = (qintptr)listenSock;

            int reuse = 1;
            setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
                       (const char*)&reuse, sizeof(reuse));

            struct sockaddr_in saddr;
            memset(&saddr, 0, sizeof(saddr));
            saddr.sin_family = AF_INET;
            saddr.sin_port   = htons((uint16_t)m_tcpPort);
            QString bindIP = (m_tcpIP.isEmpty() || m_tcpIP == "0.0.0.0")
                             ? QString("0.0.0.0") : m_tcpIP;
            saddr.sin_addr.s_addr = (bindIP == "0.0.0.0")
                                    ? htonl(INADDR_ANY)
                                    : inet_addr(bindIP.toStdString().c_str());

            if (::bind(listenSock,(struct sockaddr*)&saddr,sizeof(saddr)) < 0 ||
                ::listen(listenSock, 1) < 0) {
                emit logMessage(QString("[GATEWAY ERROR] Cannot bind %1:%2"
                                        " — use your local IP or 0.0.0.0")
                                .arg(bindIP).arg(m_tcpPort));
                gw_close(listenSock);
                m_serverSock = -1;
                goto cleanup;
            }

            emit logMessage(QString("[GATEWAY] Listening on %1:%2  <->  %3 @ %4 baud")
                            .arg(bindIP).arg(m_tcpPort).arg(m_comPort).arg(m_baudRate));

            /* 3. Accept loop */
            while (!m_shouldStop) {
                fd_set fds; struct timeval tv;
                FD_ZERO(&fds); FD_SET(listenSock, &fds);
                tv.tv_sec = 0; tv.tv_usec = 200000;
                if (select((int)listenSock + 1, &fds, NULL, NULL, &tv) <= 0) continue;

                gw_sock_t client = ::accept(listenSock, NULL, NULL);
                if (client == GW_INVALID) continue;
                emit logMessage("[GATEWAY] TCP client connected");

                /* 4. Bridge this client */
                while (!m_shouldStop) {
                    uint8_t mbap[7];
                    if (tcp_recv_all(client, mbap, 7, 2000) != 7) break;

                    int body_len   = ((int)mbap[4] << 8) | mbap[5];
                    uint8_t uid    = mbap[6];
                    int pdu_len    = body_len - 1;
                    if (pdu_len <= 0 || pdu_len > 250) break;

                    uint8_t pdu[256];
                    if (tcp_recv_all(client, pdu, pdu_len, 1000) != pdu_len) break;

                    /* Build RTU frame */
                    uint8_t rtu_req[260];
                    rtu_req[0] = uid;
                    memcpy(rtu_req + 1, pdu, pdu_len);
                    uint16_t crc = crc16(rtu_req, 1 + pdu_len);
                    rtu_req[1 + pdu_len] = crc & 0xFF;
                    rtu_req[2 + pdu_len] = (crc >> 8) & 0xFF;
                    int rtu_req_len = 3 + pdu_len;

                    uint8_t tcp_rsp[260];
                    int tcp_rsp_len = 0;

                    if (modbus_send_raw(m_gatewayRtu, rtu_req, rtu_req_len) != rtu_req_len) {
                        emit logMessage("[GATEWAY] Serial write error — sending exception");
                        /* Modbus exception: fc|0x80, exception code 04 (device failure) */
                        tcp_rsp[0]=mbap[0]; tcp_rsp[1]=mbap[1];
                        tcp_rsp[2]=0; tcp_rsp[3]=0;
                        tcp_rsp[4]=0; tcp_rsp[5]=3;   /* length = uid+fc+ec = 3 */
                        tcp_rsp[6]=uid; tcp_rsp[7]=pdu[0]|0x80; tcp_rsp[8]=0x04;
                        tcp_rsp_len = 9;
                        ::send(client, (const char*)tcp_rsp, tcp_rsp_len, 0);
                        continue;  /* keep client connected, try next request */
                    }

                    /* Read RTU response */
                    uint8_t rtu_rsp[256];
                    int rsp_len = modbus_recv_raw(m_gatewayRtu, rtu_rsp,
                                                  (int)sizeof(rtu_rsp), 1000);
                    if (rsp_len < 4) {
                        emit logMessage("[GATEWAY] RTU response timeout — sending exception");
                        tcp_rsp[0]=mbap[0]; tcp_rsp[1]=mbap[1];
                        tcp_rsp[2]=0; tcp_rsp[3]=0;
                        tcp_rsp[4]=0; tcp_rsp[5]=3;
                        tcp_rsp[6]=uid; tcp_rsp[7]=pdu[0]|0x80; tcp_rsp[8]=0x04;
                        tcp_rsp_len = 9;
                        ::send(client, (const char*)tcp_rsp, tcp_rsp_len, 0);
                        continue;
                    }
                    uint16_t cc = crc16(rtu_rsp, rsp_len - 2);
                    uint16_t cr = (uint16_t)rtu_rsp[rsp_len-2]
                                  | ((uint16_t)rtu_rsp[rsp_len-1] << 8);
                    if (cc != cr) {
                        emit logMessage("[GATEWAY] RTU CRC mismatch — sending exception");
                        tcp_rsp[0]=mbap[0]; tcp_rsp[1]=mbap[1];
                        tcp_rsp[2]=0; tcp_rsp[3]=0;
                        tcp_rsp[4]=0; tcp_rsp[5]=3;
                        tcp_rsp[6]=uid; tcp_rsp[7]=pdu[0]|0x80; tcp_rsp[8]=0x03;
                        tcp_rsp_len = 9;
                        ::send(client, (const char*)tcp_rsp, tcp_rsp_len, 0);
                        continue;
                    }

                    /* Build TCP response — forward RTU payload (strip 2-byte CRC) */
                    int resp_pdu = rsp_len - 2;  /* = unit_id + func + data */
                    tcp_rsp[0] = mbap[0]; tcp_rsp[1] = mbap[1];
                    tcp_rsp[2] = 0;       tcp_rsp[3] = 0;
                    tcp_rsp[4] = (resp_pdu >> 8) & 0xFF;
                    tcp_rsp[5] = resp_pdu & 0xFF;
                    memcpy(tcp_rsp + 6, rtu_rsp, resp_pdu);
                    ::send(client, (const char*)tcp_rsp, 6 + resp_pdu, 0);
                }
                gw_close(client);
                emit logMessage("[GATEWAY] TCP client disconnected");
            }

            gw_close(listenSock);
            m_serverSock = -1;
        }

    } else {
        /* ════ TCP-to-RTU: connect to remote server, bridge from serial ════ */

        m_gatewayRtu = modbus_new_rtu(m_comPort.toStdString().c_str(),
                                      m_baudRate, m_parity, 8, 1);
        if (!m_gatewayRtu || modbus_connect(m_gatewayRtu) != 0) {
            emit logMessage(QString("[GATEWAY ERROR] Cannot open serial port %1").arg(m_comPort));
            goto cleanup;
        }

        {
            gw_sock_t tcpSock = ::socket(AF_INET, SOCK_STREAM, 0);
            if (tcpSock == GW_INVALID) {
                emit logMessage("[GATEWAY ERROR] Failed to create TCP socket");
                goto cleanup;
            }
            struct sockaddr_in srv;
            memset(&srv, 0, sizeof(srv));
            srv.sin_family      = AF_INET;
            srv.sin_port        = htons((uint16_t)m_tcpPort);
            srv.sin_addr.s_addr = inet_addr(m_tcpIP.toStdString().c_str());

#ifdef _WIN32
            { u_long nb=1; ioctlsocket(tcpSock,FIONBIO,&nb); }
#else
            { int fl=fcntl(tcpSock,F_GETFL,0); fcntl(tcpSock,F_SETFL,fl|O_NONBLOCK); }
#endif
            ::connect(tcpSock,(struct sockaddr*)&srv,sizeof(srv));
            {
                fd_set wf; struct timeval tv;
                FD_ZERO(&wf); FD_SET(tcpSock,&wf);
                tv.tv_sec=3; tv.tv_usec=0;
                if (select((int)tcpSock+1,NULL,&wf,NULL,&tv)<=0) {
                    emit logMessage(QString("[GATEWAY ERROR] Cannot connect to %1:%2")
                                    .arg(m_tcpIP).arg(m_tcpPort));
                    gw_close(tcpSock); goto cleanup;
                }
            }
#ifdef _WIN32
            { u_long blk=0; ioctlsocket(tcpSock,FIONBIO,&blk); }
#else
            { int fl=fcntl(tcpSock,F_GETFL,0); fcntl(tcpSock,F_SETFL,fl&~O_NONBLOCK); }
#endif
            emit logMessage(QString("[GATEWAY] Connected to TCP %1:%2  <->  %3 @ %4 baud")
                            .arg(m_tcpIP).arg(m_tcpPort).arg(m_comPort).arg(m_baudRate));

            uint16_t tid = 1;
            while (!m_shouldStop) {
                uint8_t rtu_req[260];
                int rtu_len = modbus_recv_raw(m_gatewayRtu, rtu_req,
                                              (int)sizeof(rtu_req), 500);
                if (rtu_len < 4) { msleep(10); continue; }

                uint16_t cc = crc16(rtu_req, rtu_len - 2);
                uint16_t cr = (uint16_t)rtu_req[rtu_len-2]
                              | ((uint16_t)rtu_req[rtu_len-1] << 8);
                if (cc != cr) { emit logMessage("[GATEWAY] RTU request CRC error"); continue; }

                int pdu_len = rtu_len - 2;
                uint8_t tcp_req[260];
                tcp_req[0] = (tid>>8)&0xFF; tcp_req[1] = tid&0xFF;
                tcp_req[2] = 0; tcp_req[3] = 0;
                tcp_req[4] = (pdu_len>>8)&0xFF; tcp_req[5] = pdu_len&0xFF;
                memcpy(tcp_req + 6, rtu_req, pdu_len);
                tid++;

                if (::send(tcpSock,(const char*)tcp_req, 6+pdu_len, 0) < 0) {
                    emit logMessage("[GATEWAY] TCP send error"); break;
                }

                uint8_t mbap[7];
                if (tcp_recv_all(tcpSock, mbap, 7, 2000) != 7) {
                    emit logMessage("[GATEWAY] TCP response timeout"); break;
                }
                int body_len    = ((int)mbap[4]<<8)|mbap[5];
                int resp_pdu_len = body_len - 1;
                uint8_t resp_pdu[256];
                if (tcp_recv_all(tcpSock, resp_pdu, resp_pdu_len, 1000) != resp_pdu_len) {
                    emit logMessage("[GATEWAY] TCP response incomplete"); break;
                }

                uint8_t rtu_rsp[260];
                rtu_rsp[0] = mbap[6];
                memcpy(rtu_rsp+1, resp_pdu, resp_pdu_len);
                uint16_t crc = crc16(rtu_rsp, 1+resp_pdu_len);
                rtu_rsp[1+resp_pdu_len] = crc&0xFF;
                rtu_rsp[2+resp_pdu_len] = (crc>>8)&0xFF;
                modbus_send_raw(m_gatewayRtu, rtu_rsp, 3+resp_pdu_len);
            }
            gw_close(tcpSock);
        }
    }

cleanup:
    if (m_gatewayRtu) {
        modbus_close(m_gatewayRtu); modbus_free(m_gatewayRtu); m_gatewayRtu = nullptr;
    }
    if (m_gatewayTcp) {
        modbus_close(m_gatewayTcp); modbus_free(m_gatewayTcp); m_gatewayTcp = nullptr;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    emit logMessage("[GATEWAY] Thread stopped");
}
