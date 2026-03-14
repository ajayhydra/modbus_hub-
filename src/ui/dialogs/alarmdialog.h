#ifndef ALARMDIALOG_H
#define ALARMDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QGroupBox>
#include "alarm_manager.h"

class AlarmDialog : public QDialog {
    Q_OBJECT

public:
    explicit AlarmDialog(QWidget *parent = nullptr);
    
    void loadAlarmsFromManager();  // Load existing alarms
    void saveAlarmsToManager();    // Save alarms back to manager

private slots:
    void onAddAlarm();
    void onRemoveAlarm();
    void onEditAlarm();
    void onConfigureEmail();

private:
    void setupUI();
    void refreshList();

    QListWidget* m_listAlarms;
    QList<alarm_mgr_config_t> m_alarms;
};

#endif // ALARMDIALOG_H
