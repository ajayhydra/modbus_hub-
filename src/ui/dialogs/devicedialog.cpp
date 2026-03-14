#include "devicedialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QIntValidator>
#include <cstring>

DeviceDialog::DeviceDialog(QWidget *parent, modbus_device_t* device)
    : QDialog(parent)
    , m_isEditMode(device != nullptr)
{
    setWindowTitle(m_isEditMode ? "Edit Device" : "Add Device");
    setModal(true);
    setMinimumWidth(500);
    resize(500, 560);
    
    memset(&m_device, 0, sizeof(m_device));
    
    setupUI();
    
    if (device) {
        loadDeviceData(device);
    } else {
        // Set defaults for new device
        m_radioTcp->setChecked(true);
        m_spinPort->setValue(502);
        m_comboBaudRate->setCurrentText("9600");
        m_comboParity->setCurrentText("None");
        m_spinDataBits->setValue(8);
        m_spinStopBits->setValue(1);
        m_spinSlaveId->setValue(1);
        m_spinPollInterval->setValue(1000);
        m_checkEnabled->setChecked(true);
        onConnectionTypeChanged(true);
    }
}

void DeviceDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(12, 10, 12, 10);
    
    // Device name
    QFormLayout* nameLayout = new QFormLayout();
    m_editName = new QLineEdit(this);
    nameLayout->addRow("Device Name:", m_editName);
    mainLayout->addLayout(nameLayout);
    
    // Connection type
    QGroupBox* typeGroup = new QGroupBox("Connection Type", this);
    QHBoxLayout* typeLayout = new QHBoxLayout(typeGroup);
    m_radioTcp = new QRadioButton("TCP", this);
    m_radioRtu = new QRadioButton("RTU", this);
    typeLayout->addWidget(m_radioTcp);
    typeLayout->addWidget(m_radioRtu);
    mainLayout->addWidget(typeGroup);
    
    // TCP settings
    QGroupBox* tcpGroup = new QGroupBox("TCP Settings", this);
    QFormLayout* tcpLayout = new QFormLayout(tcpGroup);
    m_editIpAddress = new QLineEdit(this);
    m_editIpAddress->setPlaceholderText("192.168.1.100");
    m_spinPort = new QSpinBox(this);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(502);
    tcpLayout->addRow("IP Address:", m_editIpAddress);
    tcpLayout->addRow("Port:", m_spinPort);
    mainLayout->addWidget(tcpGroup);
    
    // RTU settings
    QGroupBox* rtuGroup = new QGroupBox("RTU Settings", this);
    QFormLayout* rtuLayout = new QFormLayout(rtuGroup);
    
    m_comboComPort = new QComboBox(this);
    m_comboComPort->setEditable(true);
#ifdef _WIN32
    for (int i = 1; i <= 16; i++) {
        m_comboComPort->addItem(QString("COM%1").arg(i));
    }
#else
    m_comboComPort->addItem("/dev/ttyUSB0");
    m_comboComPort->addItem("/dev/ttyUSB1");
    m_comboComPort->addItem("/dev/ttyS0");
    m_comboComPort->addItem("/dev/ttyS1");
#endif
    
    m_comboBaudRate = new QComboBox(this);
    m_comboBaudRate->setEditable(true);
    m_comboBaudRate->addItems({"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"});
    m_comboBaudRate->setCurrentText("9600");
    m_comboBaudRate->setValidator(new QIntValidator(300, 4000000, this));
    m_comboBaudRate->lineEdit()->setPlaceholderText("e.g. 9600");
    
    m_comboParity = new QComboBox(this);
    m_comboParity->addItem("None", 'N');
    m_comboParity->addItem("Even", 'E');
    m_comboParity->addItem("Odd", 'O');
    
    m_spinDataBits = new QSpinBox(this);
    m_spinDataBits->setRange(5, 8);
    m_spinDataBits->setValue(8);
    
    m_spinStopBits = new QSpinBox(this);
    m_spinStopBits->setRange(1, 2);
    m_spinStopBits->setValue(1);
    
    rtuLayout->addRow("COM Port:", m_comboComPort);
    rtuLayout->addRow("Baud Rate:", m_comboBaudRate);
    rtuLayout->addRow("Parity:", m_comboParity);
    rtuLayout->addRow("Data Bits:", m_spinDataBits);
    rtuLayout->addRow("Stop Bits:", m_spinStopBits);
    mainLayout->addWidget(rtuGroup);
    
    // Common settings
    QGroupBox* commonGroup = new QGroupBox("Common Settings", this);
    QFormLayout* commonLayout = new QFormLayout(commonGroup);
    
    m_spinSlaveId = new QSpinBox(this);
    m_spinSlaveId->setRange(0, 255);
    m_spinSlaveId->setValue(1);
    
    m_spinPollInterval = new QSpinBox(this);
    m_spinPollInterval->setRange(0, 60000);
    m_spinPollInterval->setSuffix(" ms");
    m_spinPollInterval->setSpecialValueText("Disabled");
    m_spinPollInterval->setValue(1000);
    
    m_checkEnabled = new QCheckBox("Enabled", this);
    m_checkEnabled->setChecked(true);
    
    commonLayout->addRow("Slave ID:", m_spinSlaveId);
    commonLayout->addRow("Poll Interval:", m_spinPollInterval);
    commonLayout->addRow("", m_checkEnabled);
    mainLayout->addWidget(commonGroup);
    
    // Register address for initial poll
    QFormLayout* regLayout = new QFormLayout();
    m_spinRegister = new QSpinBox(this);
    m_spinRegister->setRange(0, 65535);
    m_spinRegister->setValue(0);
    regLayout->addRow("Register to Poll:", m_spinRegister);
    mainLayout->addLayout(regLayout);
    
    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);
    
    // Connections
    connect(m_radioTcp, &QRadioButton::toggled, this, &DeviceDialog::onConnectionTypeChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void DeviceDialog::onConnectionTypeChanged(bool isTcp) {
    // Show only the relevant settings group; hide the other
    // Find the group boxes by their title
    for (QGroupBox* gb : findChildren<QGroupBox*>()) {
        if (gb->title() == "TCP Settings")  gb->setVisible(isTcp);
        if (gb->title() == "RTU Settings")  gb->setVisible(!isTcp);
    }

    m_editIpAddress->setEnabled(isTcp);
    m_spinPort->setEnabled(isTcp);
    m_comboComPort->setEnabled(!isTcp);
    m_comboBaudRate->setEnabled(!isTcp);
    m_comboParity->setEnabled(!isTcp);
    m_spinDataBits->setEnabled(!isTcp);
    m_spinStopBits->setEnabled(!isTcp);

    // Let the dialog shrink/grow to fit
    adjustSize();
}

void DeviceDialog::loadDeviceData(const modbus_device_t* device) {
    if (!device) return;
    
    m_device = *device;
    
    m_editName->setText(device->name);
    
    if (device->type == DEVICE_TYPE_TCP) {
        m_radioTcp->setChecked(true);
        m_editIpAddress->setText(device->ip_address);
        m_spinPort->setValue(device->port);
    } else {
        m_radioRtu->setChecked(true);
        m_comboComPort->setCurrentText(device->com_port);
        m_comboBaudRate->setCurrentText(QString::number(device->baud_rate));
        
        // Find parity in combo
        for (int i = 0; i < m_comboParity->count(); i++) {
            if (m_comboParity->itemData(i).toChar() == device->parity) {
                m_comboParity->setCurrentIndex(i);
                break;
            }
        }
        
        m_spinDataBits->setValue(device->data_bits);
        m_spinStopBits->setValue(device->stop_bits);
    }
    
    m_spinSlaveId->setValue(device->slave_id);
    m_spinPollInterval->setValue(device->poll_interval_ms);
    m_checkEnabled->setChecked(device->enabled);
    m_spinRegister->setValue(device->poll_register);
}

modbus_device_t DeviceDialog::getDeviceConfig() const {
    modbus_device_t device;
    memset(&device, 0, sizeof(device));
    
    // Copy existing ID if in edit mode
    if (m_isEditMode) {
        device.id = m_device.id;
    }
    
    // Name
    strncpy(device.name, m_editName->text().toUtf8().constData(), sizeof(device.name) - 1);
    
    // Connection type and settings
    if (m_radioTcp->isChecked()) {
        device.type = DEVICE_TYPE_TCP;
        strncpy(device.ip_address, m_editIpAddress->text().toUtf8().constData(), sizeof(device.ip_address) - 1);
        device.port = m_spinPort->value();
    } else {
        device.type = DEVICE_TYPE_RTU;
        strncpy(device.com_port, m_comboComPort->currentText().toUtf8().constData(), sizeof(device.com_port) - 1);
        device.baud_rate = m_comboBaudRate->currentText().toInt();
        device.parity = m_comboParity->currentData().toChar().toLatin1();
        device.data_bits = m_spinDataBits->value();
        device.stop_bits = m_spinStopBits->value();
    }
    
    // Common settings
    device.slave_id = m_spinSlaveId->value();
    device.poll_interval_ms = m_spinPollInterval->value();
    device.enabled = m_checkEnabled->isChecked();
    device.connected = false;
    device.ctx = nullptr;

    // Read register value directly from the visible text so that a typed
    // value that has not yet been committed via Enter/Tab is still captured.
    bool regOk = false;
    int regVal = m_spinRegister->text().toInt(&regOk);
    if (regOk && regVal >= 0 && regVal <= 65535) {
        device.poll_register = static_cast<uint16_t>(regVal);
    } else {
        device.poll_register = static_cast<uint16_t>(m_spinRegister->value());
    }

    return device;
}
