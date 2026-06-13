// Slot implementations for MainWindow
#include "mainwindow.h"
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
#include <QElapsedTimer>
#include <QTableWidgetItem>
#include <QAbstractSocket>

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
        updateStatus(QString("Connected  %1:%2").arg(ip).arg(port));
        setWindowTitle(QString("ModbusHub — %1:%2").arg(ip).arg(port));
        
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
        updateStatus(QString("Connected  %1 @ %2 baud").arg(comPort).arg(baud));
        setWindowTitle(QString("ModbusHub — %1").arg(comPort));
    }

    // Green dot — connected
    m_statusDot->setStyleSheet("background-color:#34c759;border-radius:5px;");

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
    setWindowTitle("ModbusHub — Modbus TCP/RTU Master");
    m_statusDot->setStyleSheet("background-color:#aeaeb2;border-radius:5px;");
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

    if (funcIndex == 0 || funcIndex == 1) {
        QVector<uint8_t> bits(quantity);
        if (funcIndex == 0) {
            rc = modbus_read_bits(m_modbus, address, quantity, bits.data());
        } else {
            rc = modbus_read_input_bits(m_modbus, address, quantity, bits.data());
        }

        if (rc == quantity) {
            int fc = (funcIndex == 0) ? 1 : 2;
            log(LOG_CAT_MASTER,
                QString("[READ] FC=0x%1: Read %2 items from address %3")
                    .arg(fc, 2, 16, QChar('0')).arg(quantity).arg(address));

            m_lastBits = bits;
            m_lastRegs.clear();
            m_lastReadFunc = funcIndex;
            m_lastReadAddress = address;
            renderDataView();
        } else {
            int fc = (funcIndex == 0) ? 1 : 2;
            int exc = modbus_get_last_exception(m_modbus);
            QString reason = exc ? QString("Modbus exception %1 (%2)")
                                       .arg(exc).arg(modbus_exception_string(exc))
                                 : QString::fromLatin1(modbus_strerror(errno));
            log(LOG_CAT_MASTER,
                QString("[ERROR] Read failed: FC=0x%1, Addr=%2, Qty=%3 - %4")
                    .arg(fc, 2, 16, QChar('0')).arg(address).arg(quantity).arg(reason));
        }

    } else if (funcIndex <= 3) {
        QVector<uint16_t> regs(quantity);
        if (funcIndex == 2) {
            rc = modbus_read_registers(m_modbus, address, quantity, regs.data());
        } else {
            rc = modbus_read_input_registers(m_modbus, address, quantity, regs.data());
        }

        if (rc == quantity) {
            int fc = (funcIndex == 2) ? 3 : 4;
            log(LOG_CAT_MASTER,
                QString("[READ] FC=0x%1: Read %2 items from address %3")
                    .arg(fc, 2, 16, QChar('0')).arg(quantity).arg(address));

            m_lastRegs = regs;
            m_lastBits.clear();
            m_lastReadFunc = funcIndex;
            m_lastReadAddress = address;
            renderDataView();
        } else {
            int fc = (funcIndex == 2) ? 3 : 4;
            int exc = modbus_get_last_exception(m_modbus);
            QString reason = exc ? QString("Modbus exception %1 (%2)")
                                       .arg(exc).arg(modbus_exception_string(exc))
                                 : QString::fromLatin1(modbus_strerror(errno));
            log(LOG_CAT_MASTER,
                QString("[ERROR] Read failed: FC=0x%1, Addr=%2, Qty=%3 - %4")
                    .arg(fc, 2, 16, QChar('0')).arg(address).arg(quantity).arg(reason));
        }
    }
}

// ── Type interpretation helpers ──────────────────────────────────────────
namespace {

inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

// Reorder two raw 16-bit registers into a 32-bit value according to the
// chosen word/byte ordering. r0 is the lower-address register, r1 the next.
uint32_t assemble32(uint16_t r0, uint16_t r1, int order) {
    switch (order) {
        case 1: return ((uint32_t)r1 << 16) | r0;                          // CDAB
        case 2: return ((uint32_t)bswap16(r0) << 16) | bswap16(r1);        // BADC
        case 3: return ((uint32_t)bswap16(r1) << 16) | bswap16(r0);        // DCBA
        default: return ((uint32_t)r0 << 16) | r1;                         // ABCD
    }
}

// Same idea for 4 registers → 64 bits. The register-reverse and byte-swap
// flags are decomposed identically to the 32-bit case.
uint64_t assemble64(uint16_t r0, uint16_t r1, uint16_t r2, uint16_t r3, int order) {
    uint16_t a, b, c, d;
    bool revWords  = (order == 1) || (order == 3);
    bool swapBytes = (order == 2) || (order == 3);
    if (revWords) { a = r3; b = r2; c = r1; d = r0; }
    else          { a = r0; b = r1; c = r2; d = r3; }
    if (swapBytes) { a = bswap16(a); b = bswap16(b); c = bswap16(c); d = bswap16(d); }
    return ((uint64_t)a << 48) | ((uint64_t)b << 32) | ((uint64_t)c << 16) | d;
}

QString formatInt16(int16_t v, int radix) {
    if (radix == 1) return QString("0x%1").arg((uint16_t)v, 4, 16, QChar('0')).toUpper();
    if (radix == 2) return QString::number((uint16_t)v, 8);
    return QString::number(v);
}
QString formatUInt16(uint16_t v, int radix) {
    if (radix == 1) return QString("0x%1").arg(v, 4, 16, QChar('0')).toUpper();
    if (radix == 2) return QString::number(v, 8);
    return QString::number(v);
}
QString formatInt32(int32_t v, int radix) {
    if (radix == 1) return QString("0x%1").arg((uint32_t)v, 8, 16, QChar('0')).toUpper();
    if (radix == 2) return QString::number((uint32_t)v, 8);
    return QString::number(v);
}
QString formatUInt32(uint32_t v, int radix) {
    if (radix == 1) return QString("0x%1").arg(v, 8, 16, QChar('0')).toUpper();
    if (radix == 2) return QString::number(v, 8);
    return QString::number(v);
}

} // namespace

void MainWindow::renderDataView() {
    m_textData->clear();
    if (m_lastReadFunc < 0) return;

    // Bit reads (coils / discrete inputs) ignore Type/Word Order — they are
    // just 0/1 per address.
    if (!m_lastBits.isEmpty()) {
        int baseAddr = (m_lastReadFunc == 0) ? 1 : 10001;
        for (int i = 0; i < m_lastBits.size(); i++) {
            addLogMessage(m_textData,
                QString("Address [%1]: %2")
                    .arg(baseAddr + m_lastReadAddress + i, 5, 10, QChar('0'))
                    .arg(m_lastBits[i]));
        }
        return;
    }

    if (m_lastRegs.isEmpty()) return;

    int baseAddr = (m_lastReadFunc == 2) ? 40001 : 30001;
    int n = m_lastRegs.size();
    int radix = m_displayFormat;        // 0=Dec 1=Hex 2=Oct
    int type  = m_dataType;             // 0..5
    int order = m_wordOrder;            // 0..3

    auto addrStr = [&](int regOff) {
        return QString::number(baseAddr + m_lastReadAddress + regOff).rightJustified(5, '0');
    };

    if (type == 0 || type == 1) {
        // Single-register integer types — one row per register.
        for (int i = 0; i < n; i++) {
            QString val = (type == 0) ? formatUInt16(m_lastRegs[i], radix)
                                      : formatInt16((int16_t)m_lastRegs[i], radix);
            addLogMessage(m_textData,
                QString("Address [%1]: %2").arg(addrStr(i)).arg(val));
        }
    } else if (type == 2 || type == 3 || type == 4) {
        // 32-bit types — one value per pair of registers.
        int pairs = n / 2;
        for (int i = 0; i < pairs; i++) {
            uint32_t bits = assemble32(m_lastRegs[i*2], m_lastRegs[i*2 + 1], order);
            QString val;
            if (type == 4) {
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                val = QString::number((double)f, 'g', 9);
            } else if (type == 3) {
                val = formatInt32((int32_t)bits, radix);
            } else {
                val = formatUInt32(bits, radix);
            }
            addLogMessage(m_textData,
                QString("Address [%1..%2]: %3")
                    .arg(addrStr(i*2)).arg(addrStr(i*2 + 1)).arg(val));
        }
        if (n % 2) {
            addLogMessage(m_textData,
                QString("Address [%1]: <incomplete — type needs %2 registers, %3 trailing>")
                    .arg(addrStr(pairs * 2)).arg(2).arg(n - pairs * 2));
        }
    } else { // type == 5: Float64
        int quads = n / 4;
        for (int i = 0; i < quads; i++) {
            uint64_t bits = assemble64(m_lastRegs[i*4],     m_lastRegs[i*4 + 1],
                                       m_lastRegs[i*4 + 2], m_lastRegs[i*4 + 3], order);
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            addLogMessage(m_textData,
                QString("Address [%1..%2]: %3")
                    .arg(addrStr(i*4)).arg(addrStr(i*4 + 3))
                    .arg(QString::number(d, 'g', 17)));
        }
        if (n % 4) {
            addLogMessage(m_textData,
                QString("Address [%1]: <incomplete — Float64 needs 4 registers, %2 trailing>")
                    .arg(addrStr(quads * 4)).arg(n - quads * 4));
        }
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
    // Re-render cached data (if any) without re-fetching from the slave.
    renderDataView();
}

void MainWindow::onTypeChanged(int index) {
    m_dataType = index;
    // Word-order is only meaningful for multi-register types (UInt32+).
    bool multiReg = (index >= 2);
    m_comboWordOrder->setEnabled(multiReg);
    renderDataView();
}

void MainWindow::onWordOrderChanged(int index) {
    m_wordOrder = index;
    renderDataView();
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

void MainWindow::onScanNetwork() {
    QString startIP = m_editScanStartIP->text().trimmed();
    QString endIP   = m_editScanEndIP->text().trimmed();
    int     port    = m_editScanPort->text().toInt();

    QHostAddress startAddr(startIP);
    QHostAddress endAddr(endIP);

    if (startAddr.isNull() || endAddr.isNull() ||
        startAddr.protocol() != QAbstractSocket::IPv4Protocol ||
        endAddr.protocol()   != QAbstractSocket::IPv4Protocol) {
        QMessageBox::warning(this, "Network Scanner", "Invalid IP address format. Use IPv4 (e.g. 192.168.1.1).");
        return;
    }
    if (startAddr.toIPv4Address() > endAddr.toIPv4Address()) {
        QMessageBox::warning(this, "Network Scanner", "Start IP must be less than or equal to End IP.");
        return;
    }
    if (port < 1 || port > 65535) {
        QMessageBox::warning(this, "Network Scanner", "Port must be between 1 and 65535.");
        return;
    }

    // Stop any previous scan
    if (m_scanWorker && m_scanWorker->isRunning()) {
        m_scanWorker->stop();
        m_scanWorker->wait();
    }
    delete m_scanWorker;
    m_scanWorker = nullptr;

    // Clear previous results
    m_tableScanResults->setRowCount(0);
    m_scanProgressBar->setValue(0);
    m_scanProgressBar->setVisible(true);
    m_scanStatusLabel->setText(QString("Scanning %1 → %2 ...").arg(startIP, endIP));

    m_scanningNetwork = true;
    m_btnScanNetwork->setEnabled(false);
    m_btnStopScan->setEnabled(true);

    log(LOG_CAT_SCANNER, QString("[SCAN] Starting background scan %1 → %2 port %3")
        .arg(startIP, endIP).arg(port));

    m_scanWorker = new ScanWorkerThread(this);
    connect(m_scanWorker, &ScanWorkerThread::resultFound,
            this, &MainWindow::onScanResultFound, Qt::QueuedConnection);
    connect(m_scanWorker, &ScanWorkerThread::progress,
            this, &MainWindow::onScanProgress, Qt::QueuedConnection);
    connect(m_scanWorker, &ScanWorkerThread::finished,
            this, &MainWindow::onScanFinished, Qt::QueuedConnection);
    m_scanWorker->startScan(startIP, endIP, port);
}

void MainWindow::onStopScan() {
    if (m_scanWorker && m_scanWorker->isRunning()) {
        m_scanWorker->stop();
        // Don't wait here — onScanFinished will tidy up via the signal
    }
    m_scanningNetwork = false;
    m_btnScanNetwork->setEnabled(true);
    m_btnStopScan->setEnabled(false);
    m_scanProgressBar->setVisible(false);
    m_scanStatusLabel->setText("Scan stopped by user.");
    log(LOG_CAT_SCANNER, "[SCAN] Network scan stopped by user");
}

void MainWindow::onScanResultFound(const QString &ip, int port, int latencyMs) {
    int row = m_tableScanResults->rowCount();
    m_tableScanResults->insertRow(row);

    auto *ipItem  = new QTableWidgetItem(ip);
    auto *portItem = new QTableWidgetItem(QString::number(port));
    auto *latItem  = new QTableWidgetItem(QString("%1 ms").arg(latencyMs));

    portItem->setTextAlignment(Qt::AlignCenter);
    latItem->setTextAlignment(Qt::AlignCenter);

    m_tableScanResults->setItem(row, 0, ipItem);
    m_tableScanResults->setItem(row, 1, portItem);
    m_tableScanResults->setItem(row, 2, latItem);

    log(LOG_CAT_SCANNER, QString("[SCAN] Found Modbus device at %1:%2 (%3 ms)").arg(ip).arg(port).arg(latencyMs));
}

void MainWindow::onScanProgress(int current, int total) {
    if (total > 0) {
        int pct = (current * 100) / total;
        m_scanProgressBar->setValue(pct);

        // Reconstruct current IP string from base + offset for display
        quint32 base = QHostAddress(m_editScanStartIP->text().trimmed()).toIPv4Address();
        QHostAddress cur(base + static_cast<quint32>(current - 1));
        m_scanStatusLabel->setText(QString("Scanning %1  (%2 / %3)").arg(cur.toString()).arg(current).arg(total));
    }
}

void MainWindow::onScanFinished(int found) {
    m_scanningNetwork = false;
    m_btnScanNetwork->setEnabled(true);
    m_btnStopScan->setEnabled(false);
    m_scanProgressBar->setValue(100);
    m_scanProgressBar->setVisible(false);
    m_scanStatusLabel->setText(QString("Scan complete — %1 device(s) found.").arg(found));
    log(LOG_CAT_SCANNER, QString("[SCAN] Scan complete. Found %1 device(s).").arg(found));
    updateStatus(QString("Network scan complete: %1 device(s) found").arg(found));
}

void MainWindow::onScanResultDoubleClicked(int row, int /*column*/) {
    QString ip   = m_tableScanResults->item(row, 0)->text();
    QString portStr = m_tableScanResults->item(row, 1)->text();

    // Pre-fill the Add Device dialog with the scanned IP
    m_editScanStartIP->setText(ip);  // keep for reference
    // Switch to master tab and pre-fill connection fields
    m_editIP->setText(ip);
    m_editPort->setText(portStr);
    m_comboMode->setCurrentIndex(0); // TCP
    m_tabWidget->setCurrentIndex(0); // jump to Master tab
    log(LOG_CAT_SCANNER, QString("[SCAN] Loaded %1:%2 into Master tab — press Connect to proceed.").arg(ip, portStr));
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
    m_statusTime->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
}

void MainWindow::enableConnectionControls(bool connected) {
    m_btnConnect->setEnabled(!connected);
    m_btnDisconnect->setEnabled(connected);
    m_btnRead->setEnabled(connected && m_comboFunction->currentIndex() <= 3);
    m_btnWrite->setEnabled(connected && m_comboFunction->currentIndex() >= 4);
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

/* ══════════════════════════════════════════════════════════════════════════
   ScanWorkerThread — IP range scanner running fully off the UI thread.
   Uses non-blocking QTcpSocket per host so the UI stays responsive and
   the scan can be cancelled at any time via stop().
   ══════════════════════════════════════════════════════════════════════════ */

ScanWorkerThread::ScanWorkerThread(QObject *parent)
    : QThread(parent), m_shouldStop(0), m_port(502), m_timeoutMs(200)
{
}

void ScanWorkerThread::startScan(const QString &startIP, const QString &endIP,
                                  int port, int timeoutMs)
{
    m_startIP   = startIP;
    m_endIP     = endIP;
    m_port      = port;
    m_timeoutMs = timeoutMs;
    m_shouldStop.storeRelaxed(0);
    start();
}

void ScanWorkerThread::stop()
{
    m_shouldStop.storeRelaxed(1);
}

void ScanWorkerThread::run()
{
    quint32 startAddr = QHostAddress(m_startIP).toIPv4Address();
    quint32 endAddr   = QHostAddress(m_endIP).toIPv4Address();
    int total = static_cast<int>(endAddr - startAddr + 1);
    int found = 0;

    for (quint32 ip = startAddr; ip <= endAddr && !m_shouldStop.loadRelaxed(); ++ip) {
        int current = static_cast<int>(ip - startAddr + 1);
        emit progress(current, total);

        QHostAddress addr(ip);
        QTcpSocket sock;
        sock.connectToHost(addr, static_cast<quint16>(m_port));

        QElapsedTimer timer;
        timer.start();

        if (sock.waitForConnected(m_timeoutMs)) {
            int latency = static_cast<int>(timer.elapsed());
            sock.disconnectFromHost();
            ++found;
            emit resultFound(addr.toString(), m_port, latency);
        }
    }

    emit finished(found);
}
