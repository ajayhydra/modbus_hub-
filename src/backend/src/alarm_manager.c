#include "alarm_manager.h"
#include "app_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Alarm event callback pointer
static alarm_event_callback_t g_alarm_event_cb = NULL;

void alarm_manager_set_event_callback(alarm_event_callback_t cb) {
    g_alarm_event_cb = cb;
}

// Static storage for alarms
static alarm_mgr_config_t g_alarms[ALARM_MGR_MAX_ALARMS];
static int g_alarm_count = 0;
static int g_next_alarm_id = 1;
static email_config_t g_email_config = {0};

void alarm_manager_init(void) {
    memset(g_alarms, 0, sizeof(g_alarms));
    g_alarm_count = 0;
    g_next_alarm_id = 1;
    
    // Initialize default email config
    g_email_config.enabled = false;
    g_email_config.smtp_port = 587;
    g_email_config.use_tls = true;
    
    APP_LOG_INFO("AlarmManager", "Alarm manager initialized with max %d alarms", ALARM_MGR_MAX_ALARMS);
}

int alarm_manager_add(const alarm_mgr_config_t* alarm) {
    if (!alarm) {
        APP_LOG_ERROR("AlarmManager", "Cannot add NULL alarm");
        return -1;
    }
    
    if (g_alarm_count >= ALARM_MGR_MAX_ALARMS) {
        APP_LOG_ERROR("AlarmManager", "Maximum alarm count reached (%d)", ALARM_MGR_MAX_ALARMS);
        return -1;
    }
    
    // Copy alarm and assign ID
    g_alarms[g_alarm_count] = *alarm;
    g_alarms[g_alarm_count].id = g_next_alarm_id++;
    g_alarms[g_alarm_count].last_value_set = false;
    g_alarms[g_alarm_count].last_triggered = 0;
    g_alarms[g_alarm_count].trigger_count = 0;
    
    int id = g_alarms[g_alarm_count].id;
    g_alarm_count++;
    
    APP_LOG_INFO("AlarmManager", "Added alarm #%d: %s (Dev%d Reg%d)", 
                 id, alarm->name, alarm->device_id, alarm->register_address);
    return id;
}

int alarm_manager_remove(int alarm_id) {
    for (int i = 0; i < g_alarm_count; i++) {
        if (g_alarms[i].id == alarm_id) {
            // Shift remaining alarms
            for (int j = i; j < g_alarm_count - 1; j++) {
                g_alarms[j] = g_alarms[j + 1];
            }
            g_alarm_count--;
            APP_LOG_INFO("AlarmManager", "Removed alarm #%d", alarm_id);
            return 0;
        }
    }
    APP_LOG_WARN("AlarmManager", "Alarm #%d not found for removal", alarm_id);
    return -1;
}

int alarm_manager_update(int alarm_id, const alarm_mgr_config_t* alarm) {
    if (!alarm) return -1;
    
    for (int i = 0; i < g_alarm_count; i++) {
        if (g_alarms[i].id == alarm_id) {
            // Preserve ID and state
            int old_id = g_alarms[i].id;
            int last_value = g_alarms[i].last_value;
            bool last_value_set = g_alarms[i].last_value_set;
            time_t last_triggered = g_alarms[i].last_triggered;
            uint32_t trigger_count = g_alarms[i].trigger_count;
            
            g_alarms[i] = *alarm;
            g_alarms[i].id = old_id;
            g_alarms[i].last_value = last_value;
            g_alarms[i].last_value_set = last_value_set;
            g_alarms[i].last_triggered = last_triggered;
            g_alarms[i].trigger_count = trigger_count;
            
            APP_LOG_INFO("AlarmManager", "Updated alarm #%d", alarm_id);
            return 0;
        }
    }
    return -1;
}

alarm_mgr_config_t* alarm_manager_get(int alarm_id) {
    for (int i = 0; i < g_alarm_count; i++) {
        if (g_alarms[i].id == alarm_id) {
            return &g_alarms[i];
        }
    }
    return NULL;
}

int alarm_manager_get_count(void) {
    return g_alarm_count;
}

alarm_mgr_config_t* alarm_manager_get_by_index(int index) {
    if (index >= 0 && index < g_alarm_count) {
        return &g_alarms[index];
    }
    return NULL;
}

void alarm_manager_clear_all(void) {
    g_alarm_count = 0;
    APP_LOG_INFO("AlarmManager", "Cleared all alarms");
}

void alarm_manager_enable_all(bool enable) {
    for (int i = 0; i < g_alarm_count; i++) {
        g_alarms[i].enabled = enable;
    }
    APP_LOG_INFO("AlarmManager", "Set all alarms enabled=%d", enable);
}

// Check if alarm condition is met
static bool check_condition(alarm_mgr_config_t* alarm, int value) {
    switch (alarm->condition) {
        case ALARM_CONDITION_GREATER_EQUAL:
            return value >= alarm->threshold;
        case ALARM_CONDITION_LESS_EQUAL:
            return value <= alarm->threshold;
        case ALARM_CONDITION_EQUAL:
            return value == alarm->threshold;
        case ALARM_CONDITION_NOT_EQUAL:
            return value != alarm->threshold;
        case ALARM_CONDITION_CHANGE:
            if (!alarm->last_value_set) {
                return false;
            }
            return value != alarm->last_value;
        default:
            return false;
    }
}

// Trigger alarm action
static void trigger_alarm(alarm_mgr_config_t* alarm, int value) {
    time_t now = time(NULL);
    alarm->last_triggered = now;
    alarm->trigger_count++;
    
    char message[512];
    snprintf(message, sizeof(message), 
             "ALARM TRIGGERED: %s (Dev%d Reg%d) - Value: %d, Threshold: %d",
             alarm->name, alarm->device_id, alarm->register_address,
             value, alarm->threshold);
    // Call event callback if set
    if (g_alarm_event_cb) {
        g_alarm_event_cb(message, alarm->device_id, alarm->register_address, value, alarm->threshold);
    }
    
    switch (alarm->action) {
        case ALARM_ACTION_LOG:
            APP_LOG_WARN("AlarmManager", "%s", message);
            break;
            
        case ALARM_ACTION_EMAIL:
            APP_LOG_WARN("AlarmManager", "%s (sending email)", message);
            if (g_email_config.enabled) {
                char subject[128];
                snprintf(subject, sizeof(subject), "Modbus Alarm: %s", alarm->name);
                alarm_manager_send_email(subject, message);
            } else {
                APP_LOG_WARN("AlarmManager", "Email not configured, alarm not sent");
            }
            break;
            
        case ALARM_ACTION_SOUND:
            APP_LOG_WARN("AlarmManager", "%s (sound notification)", message);
            // Sound notification could be implemented here
            break;
            
        default:
            break;
    }
}

void alarm_manager_check_value(int device_id, int register_address, int value) {
    for (int i = 0; i < g_alarm_count; i++) {
        alarm_mgr_config_t* alarm = &g_alarms[i];
        
        if (!alarm->enabled) {
            continue;
        }
        
        if (alarm->device_id != device_id || alarm->register_address != register_address) {
            continue;
        }
        
        bool condition_met = check_condition(alarm, value);
        
        // Trigger only on LOW->HIGH transition (condition becomes true)
        // This prevents the alarm from spamming every poll cycle
        if (condition_met && !alarm->is_active) {
            alarm->is_active = true;
            trigger_alarm(alarm, value);
        } else if (!condition_met && alarm->is_active) {
            // Condition cleared – reset so it can fire again next time
            alarm->is_active = false;
        }
        
        // Update last value for CHANGE detection
        alarm->last_value = value;
        alarm->last_value_set = true;
    }
}

email_config_t* alarm_manager_get_email_config(void) {
    return &g_email_config;
}

void alarm_manager_set_email_config(const email_config_t* config) {
    if (config) {
        g_email_config = *config;
        APP_LOG_INFO("AlarmManager", "Email configuration updated (server: %s:%d)", 
                     config->smtp_server, config->smtp_port);
    }
}

int alarm_manager_send_email(const char* subject, const char* body) {
    if (!g_email_config.enabled) {
        APP_LOG_WARN("AlarmManager", "Email not enabled");
        return -1;
    }
    
    // Simple implementation using system mail command (Linux)
    // For production, use a proper SMTP library
    
#ifdef _WIN32
    APP_LOG_WARN("AlarmManager", "Email sending not implemented on Windows");
    return -1;
#else
    char command[1024];
    snprintf(command, sizeof(command),
             "echo '%s' | mail -s '%s' -r '%s' '%s'",
             body, subject, g_email_config.from_email, g_email_config.to_email);
    
    int result = system(command);
    if (result == 0) {
        APP_LOG_INFO("AlarmManager", "Email sent successfully");
        return 0;
    } else {
        APP_LOG_ERROR("AlarmManager", "Failed to send email (result=%d)", result);
        return -1;
    }
#endif
}
