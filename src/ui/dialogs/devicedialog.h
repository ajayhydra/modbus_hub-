#ifndef DEVICEDIALOG_H
#define DEVICEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include "device_manager.h"

class DeviceDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceDialog(QWidget *parent = nullptr, modbus_device_t* device = nullptr);
    
    modbus_device_t getDeviceConfig() const;
    int getPollRegister() const { return m_spinRegister ? m_spinRegister->value() : 0; }

private slots:
    void onConnectionTypeChanged(bool isTcp);

private:
    void setupUI();
    void loadDeviceData(const modbus_device_t* device);

    // Input widgets
    QLineEdit* m_editName;
    QRadioButton* m_radioTcp;
    QRadioButton* m_radioRtu;
    
    // TCP settings
    QLineEdit* m_editIpAddress;
    QSpinBox* m_spinPort;
    
    // RTU settings
    QComboBox* m_comboComPort;
    QComboBox* m_comboBaudRate;
    QComboBox* m_comboParity;
    QSpinBox* m_spinDataBits;
    QSpinBox* m_spinStopBits;
    
    // Common settings
    QSpinBox* m_spinSlaveId;
    QSpinBox* m_spinPollInterval;
    QCheckBox* m_checkEnabled;
    QSpinBox* m_spinRegister;

    modbus_device_t m_device;
    bool m_isEditMode;
};

#endif // DEVICEDIALOG_H
