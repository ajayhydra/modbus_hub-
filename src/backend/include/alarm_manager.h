#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Use different prefix to avoid conflict with data_logger.h
#define ALARM_MGR_MAX_ALARMS 50

typedef enum {
    ALARM_CONDITION_GREATER_EQUAL = 0,  // >=
    ALARM_CONDITION_LESS_EQUAL,         // <=
    ALARM_CONDITION_EQUAL,              // ==
    ALARM_CONDITION_NOT_EQUAL,          // !=
    ALARM_CONDITION_CHANGE              // Value changed
} alarm_condition_t;

typedef enum {
    ALARM_ACTION_LOG = 0,
    ALARM_ACTION_EMAIL,
    ALARM_ACTION_SOUND
} alarm_action_t;

typedef struct {
    int id;
    char name[64];
    int device_id;
    int register_address;
    alarm_condition_t condition;
    int threshold;
    bool enabled;
    alarm_action_t action;
    
    // State tracking
    int last_value;
    bool last_value_set;
    bool is_active;           // true while condition is currently met
    time_t last_triggered;
    uint32_t trigger_count;
} alarm_mgr_config_t;

// Initialize alarm manager
void alarm_manager_init(void);

// Alarm CRUD operations
int alarm_manager_add(const alarm_mgr_config_t* alarm);
int alarm_manager_remove(int alarm_id);
int alarm_manager_update(int alarm_id, const alarm_mgr_config_t* alarm);
alarm_mgr_config_t* alarm_manager_get(int alarm_id);
int alarm_manager_get_count(void);
alarm_mgr_config_t* alarm_manager_get_by_index(int index);
void alarm_manager_clear_all(void);

// Alarm monitoring
void alarm_manager_check_value(int device_id, int register_address, int value);
void alarm_manager_enable_all(bool enable);

// Alarm event callback
typedef void (*alarm_event_callback_t)(const char* message, int device_id, int register_address, int value, int threshold);
void alarm_manager_set_event_callback(alarm_event_callback_t cb);

// Email configuration
typedef struct {
    bool enabled;
    char smtp_server[128];
    int smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char from_email[64];
    char to_email[256];  // Can be comma-separated list
    bool use_tls;
} email_config_t;

email_config_t* alarm_manager_get_email_config(void);
void alarm_manager_set_email_config(const email_config_t* config);
int alarm_manager_send_email(const char* subject, const char* body);

#ifdef __cplusplus
}
#endif

#endif // ALARM_MANAGER_H
