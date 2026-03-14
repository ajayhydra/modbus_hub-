#include "alarmdialog.h"
#include "device_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <cstring>

AlarmDialog::AlarmDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Alarm Configuration");
    setModal(true);
    resize(700, 500);
    
    setupUI();
    loadAlarmsFromManager();
}

void AlarmDialog::loadAlarmsFromManager() {
    m_alarms.clear();
    int count = alarm_manager_get_count();
    for (int i = 0; i < count; i++) {
        alarm_mgr_config_t* alarm = alarm_manager_get_by_index(i);
        if (alarm) {
            m_alarms.append(*alarm);
        }
    }
    refreshList();
}

void AlarmDialog::saveAlarmsToManager() {
    // Clear existing alarms
    alarm_manager_clear_all();
    
    // Add all alarms from dialog
    for (const alarm_mgr_config_t& alarm : m_alarms) {
        alarm_manager_add(&alarm);
    }
}

void AlarmDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Info label
    QLabel* infoLabel = new QLabel(
        "Configure alarms to monitor register values and trigger actions.", this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);
    
    // List of alarms
    QGroupBox* listGroup = new QGroupBox("Configured Alarms", this);
    QVBoxLayout* listLayout = new QVBoxLayout(listGroup);
    
    m_listAlarms = new QListWidget(this);
    listLayout->addWidget(m_listAlarms);
    
    // Buttons for list management
    QHBoxLayout* listButtonLayout = new QHBoxLayout();
    QPushButton* btnAdd = new QPushButton(QIcon(":/icons/icons/add.svg"), "Add Alarm", this);
    QPushButton* btnEdit = new QPushButton(QIcon(":/icons/icons/edit.svg"), "Edit", this);
    QPushButton* btnRemove = new QPushButton(QIcon(":/icons/icons/remove.svg"), "Remove", this);
    QPushButton* btnEmail = new QPushButton(QIcon(":/icons/icons/email.svg"), "Configure Email", this);
    listButtonLayout->addWidget(btnAdd);
    listButtonLayout->addWidget(btnEdit);
    listButtonLayout->addWidget(btnRemove);
    listButtonLayout->addWidget(btnEmail);
    listButtonLayout->addStretch();
    listLayout->addLayout(listButtonLayout);
    
    mainLayout->addWidget(listGroup);
    
    // Dialog buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);
    
    // Connections
    connect(btnAdd, &QPushButton::clicked, this, &AlarmDialog::onAddAlarm);
    connect(btnEdit, &QPushButton::clicked, this, &AlarmDialog::onEditAlarm);
    connect(btnRemove, &QPushButton::clicked, this, &AlarmDialog::onRemoveAlarm);
    connect(btnEmail, &QPushButton::clicked, this, &AlarmDialog::onConfigureEmail);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        saveAlarmsToManager();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    refreshList();
}

void AlarmDialog::onAddAlarm() {
    // Create a simple dialog for adding alarm
    QDialog dialog(this);
    dialog.setWindowTitle("Add Alarm");
    
    QFormLayout* layout = new QFormLayout(&dialog);
    
    QLineEdit* editName = new QLineEdit(&dialog);
    QSpinBox* spinDeviceId = new QSpinBox(&dialog);
    spinDeviceId->setRange(0, 255);
    QSpinBox* spinRegister = new QSpinBox(&dialog);
    spinRegister->setRange(0, 65535);
    QComboBox* comboCondition = new QComboBox(&dialog);
    comboCondition->addItems({">=", "<=", "==", "!=", "CHANGE"});
    QSpinBox* spinThreshold = new QSpinBox(&dialog);
    spinThreshold->setRange(-32768, 32767);
    QComboBox* comboAction = new QComboBox(&dialog);
    comboAction->addItems({"Log", "Email", "Sound"});
    QCheckBox* checkEnabled = new QCheckBox("Enabled", &dialog);
    checkEnabled->setChecked(true);
    
    layout->addRow("Alarm Name:", editName);
    layout->addRow("Device ID:", spinDeviceId);
    layout->addRow("Register Address:", spinRegister);
    layout->addRow("Condition:", comboCondition);
    layout->addRow("Threshold Value:", spinThreshold);
    layout->addRow("Action:", comboAction);
    layout->addRow("", checkEnabled);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        alarm_mgr_config_t alarm;
        memset(&alarm, 0, sizeof(alarm));
        
        strncpy(alarm.name, editName->text().toUtf8().constData(), sizeof(alarm.name) - 1);
        alarm.device_id = spinDeviceId->value();
        alarm.register_address = spinRegister->value();
        
        // Map condition string to enum
        QString cond = comboCondition->currentText();
        if (cond == ">=") alarm.condition = ALARM_CONDITION_GREATER_EQUAL;
        else if (cond == "<=") alarm.condition = ALARM_CONDITION_LESS_EQUAL;
        else if (cond == "==") alarm.condition = ALARM_CONDITION_EQUAL;
        else if (cond == "!=") alarm.condition = ALARM_CONDITION_NOT_EQUAL;
        else if (cond == "CHANGE") alarm.condition = ALARM_CONDITION_CHANGE;
        
        alarm.threshold = spinThreshold->value();
        
        // Map action string to enum
        QString act = comboAction->currentText();
        if (act == "Log") alarm.action = ALARM_ACTION_LOG;
        else if (act == "Email") alarm.action = ALARM_ACTION_EMAIL;
        else if (act == "Sound") alarm.action = ALARM_ACTION_SOUND;
        
        alarm.enabled = checkEnabled->isChecked();
        alarm.last_value_set = false;
        alarm.trigger_count = 0;
        
        m_alarms.append(alarm);
        refreshList();
    }
}

void AlarmDialog::onRemoveAlarm() {
    int row = m_listAlarms->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Remove Alarm", "Please select an alarm to remove.");
        return;
    }
    
    m_alarms.removeAt(row);
    refreshList();
}

void AlarmDialog::onEditAlarm() {
    int row = m_listAlarms->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Edit Alarm", "Please select an alarm to edit.");
        return;
    }
    
    alarm_mgr_config_t& alarm = m_alarms[row];
    
    QDialog dialog(this);
    dialog.setWindowTitle("Edit Alarm");
    
    QFormLayout* layout = new QFormLayout(&dialog);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    QLineEdit* editName = new QLineEdit(alarm.name, &dialog);

    // Device combo
    QComboBox* comboDevice = new QComboBox(&dialog);
    int deviceCount = device_manager_get_count();
    int selIdx = 0;
    for (int i = 0; i < deviceCount; i++) {
        modbus_device_t* dev = device_manager_get_by_index(i);
        if (dev) {
            comboDevice->addItem(QString("%1  (ID %2)").arg(dev->name).arg(dev->id), dev->id);
            if (dev->id == alarm.device_id) selIdx = comboDevice->count() - 1;
        }
    }
    if (comboDevice->count() == 0) comboDevice->addItem("No devices", -1);
    else comboDevice->setCurrentIndex(selIdx);

    QSpinBox* spinRegister = new QSpinBox(&dialog);
    spinRegister->setValue(alarm.register_address);
    spinRegister->setRange(0, 65535);
    QComboBox* comboCondition = new QComboBox(&dialog);
    comboCondition->addItems({">=" , "<=", "==", "!=", "CHANGE"});
    switch (alarm.condition) {
        case ALARM_CONDITION_GREATER_EQUAL: comboCondition->setCurrentText(">="); break;
        case ALARM_CONDITION_LESS_EQUAL:    comboCondition->setCurrentText("<="); break;
        case ALARM_CONDITION_EQUAL:         comboCondition->setCurrentText("=="); break;
        case ALARM_CONDITION_NOT_EQUAL:     comboCondition->setCurrentText("!="); break;
        case ALARM_CONDITION_CHANGE:        comboCondition->setCurrentText("CHANGE"); break;
    }
    QSpinBox* spinThreshold = new QSpinBox(&dialog);
    spinThreshold->setValue(alarm.threshold);
    spinThreshold->setRange(-32768, 32767);
    QComboBox* comboAction = new QComboBox(&dialog);
    comboAction->addItems({"Log", "Email", "Sound"});
    switch (alarm.action) {
        case ALARM_ACTION_LOG:   comboAction->setCurrentText("Log");   break;
        case ALARM_ACTION_EMAIL: comboAction->setCurrentText("Email"); break;
        case ALARM_ACTION_SOUND: comboAction->setCurrentText("Sound"); break;
    }
    QCheckBox* checkEnabled = new QCheckBox("Enabled", &dialog);
    checkEnabled->setChecked(alarm.enabled);
    
    layout->addRow("Alarm Name:",      editName);
    layout->addRow("Device:",          comboDevice);
    layout->addRow("Register Address:",spinRegister);
    layout->addRow("Condition:",       comboCondition);
    layout->addRow("Threshold Value:", spinThreshold);
    layout->addRow("Action:",          comboAction);
    layout->addRow("",                 checkEnabled);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        strncpy(alarm.name, editName->text().toUtf8().constData(), sizeof(alarm.name) - 1);
        alarm.device_id = comboDevice->currentData().toInt();

        bool regOk = false;
        int regVal = spinRegister->text().toInt(&regOk);
        alarm.register_address = (regOk && regVal >= 0 && regVal <= 65535) ? regVal : spinRegister->value();
        
        QString cond = comboCondition->currentText();
        if (cond == ">=") alarm.condition = ALARM_CONDITION_GREATER_EQUAL;
        else if (cond == "<=") alarm.condition = ALARM_CONDITION_LESS_EQUAL;
        else if (cond == "==") alarm.condition = ALARM_CONDITION_EQUAL;
        else if (cond == "!=") alarm.condition = ALARM_CONDITION_NOT_EQUAL;
        else if (cond == "CHANGE") alarm.condition = ALARM_CONDITION_CHANGE;
        
        alarm.threshold = spinThreshold->value();
        
        QString act = comboAction->currentText();
        if (act == "Log") alarm.action = ALARM_ACTION_LOG;
        else if (act == "Email") alarm.action = ALARM_ACTION_EMAIL;
        else if (act == "Sound") alarm.action = ALARM_ACTION_SOUND;
        
        alarm.enabled = checkEnabled->isChecked();
        // Reset so alarm re-evaluates cleanly after edit
        alarm.is_active = false;
        alarm.last_value_set = false;
        
        refreshList();
    }
}
void AlarmDialog::refreshList() {
    m_listAlarms->clear();
    
    for (const alarm_mgr_config_t& alarm : m_alarms) {
        QString condition;
        switch (alarm.condition) {
            case ALARM_CONDITION_GREATER_EQUAL: condition = ">="; break;
            case ALARM_CONDITION_LESS_EQUAL:    condition = "<="; break;
            case ALARM_CONDITION_EQUAL:         condition = "=="; break;
            case ALARM_CONDITION_NOT_EQUAL:     condition = "!="; break;
            case ALARM_CONDITION_CHANGE:        condition = "CHANGE"; break;
            default: condition = "?"; break;
        }
        
        QString action;
        switch (alarm.action) {
            case ALARM_ACTION_LOG:   action = "Log";   break;
            case ALARM_ACTION_EMAIL: action = "Email"; break;
            case ALARM_ACTION_SOUND: action = "Sound"; break;
            default: action = "?"; break;
        }

        // Show device name if available, otherwise fall back to ID
        QString devLabel;
        modbus_device_t* dev = device_manager_get(alarm.device_id);
        if (dev) devLabel = QString("%1").arg(dev->name);
        else     devLabel = QString("Dev%1").arg(alarm.device_id);

        QString text = QString("[%1]  %2  Reg%3 %4 %5  \u2192 %6  [%7]")
            .arg(alarm.enabled ? "ON" : "OFF")
            .arg(alarm.name)
            .arg(alarm.register_address)
            .arg(condition)
            .arg(alarm.threshold)
            .arg(action)
            .arg(devLabel);
        
        m_listAlarms->addItem(text);
    }
}

void AlarmDialog::onConfigureEmail() {
    // Get current email configuration
    email_config_t* current_config = alarm_manager_get_email_config();
    
    // Create email configuration dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Configure Email Notifications");
    dialog.resize(400, 300);
    
    QFormLayout* layout = new QFormLayout(&dialog);
    
    QCheckBox* checkEnabled = new QCheckBox("Enable Email Notifications", &dialog);
    checkEnabled->setChecked(current_config ? current_config->enabled : false);
    
    QLineEdit* editSmtpServer = new QLineEdit(&dialog);
    if (current_config) editSmtpServer->setText(current_config->smtp_server);
    
    QSpinBox* spinSmtpPort = new QSpinBox(&dialog);
    spinSmtpPort->setRange(1, 65535);
    spinSmtpPort->setValue(current_config ? current_config->smtp_port : 587);
    
    QLineEdit* editFromEmail = new QLineEdit(&dialog);
    if (current_config) editFromEmail->setText(current_config->from_email);
    
    QLineEdit* editToEmail = new QLineEdit(&dialog);
    if (current_config) editToEmail->setText(current_config->to_email);
    
    QLineEdit* editUsername = new QLineEdit(&dialog);
    if (current_config) editUsername->setText(current_config->smtp_username);
    
    QLineEdit* editPassword = new QLineEdit(&dialog);
    editPassword->setEchoMode(QLineEdit::Password);
    if (current_config) editPassword->setText(current_config->smtp_password);
    
    QCheckBox* checkUseTls = new QCheckBox("Use TLS", &dialog);
    checkUseTls->setChecked(current_config ? current_config->use_tls : true);
    
    layout->addRow("", checkEnabled);
    layout->addRow("SMTP Server:", editSmtpServer);
    layout->addRow("SMTP Port:", spinSmtpPort);
    layout->addRow("From Email:", editFromEmail);
    layout->addRow("To Email:", editToEmail);
    layout->addRow("Username:", editUsername);
    layout->addRow("Password:", editPassword);
    layout->addRow("", checkUseTls);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        email_config_t config;
        memset(&config, 0, sizeof(config));
        
        config.enabled = checkEnabled->isChecked();
        strncpy(config.smtp_server, editSmtpServer->text().toUtf8().constData(), sizeof(config.smtp_server) - 1);
        config.smtp_port = spinSmtpPort->value();
        strncpy(config.from_email, editFromEmail->text().toUtf8().constData(), sizeof(config.from_email) - 1);
        strncpy(config.to_email, editToEmail->text().toUtf8().constData(), sizeof(config.to_email) - 1);
        strncpy(config.smtp_username, editUsername->text().toUtf8().constData(), sizeof(config.smtp_username) - 1);
        strncpy(config.smtp_password, editPassword->text().toUtf8().constData(), sizeof(config.smtp_password) - 1);
        config.use_tls = checkUseTls->isChecked();
        
        alarm_manager_set_email_config(&config);
        
        QMessageBox::information(this, "Email Configuration", "Email settings saved successfully.");
    }
}
