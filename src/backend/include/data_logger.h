#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Log entry types
typedef enum {
    LOG_TYPE_COIL,
    LOG_TYPE_DISCRETE_INPUT,
    LOG_TYPE_HOLDING_REGISTER,
    LOG_TYPE_INPUT_REGISTER,
    LOG_TYPE_EVENT,
    LOG_TYPE_ALARM
} log_entry_type_t;

// Data quality flags
typedef enum {
    QUALITY_GOOD = 0,
    QUALITY_BAD = 1,
    QUALITY_UNCERTAIN = 2,
    QUALITY_COMM_ERROR = 3
} data_quality_t;

// Log entry structure
typedef struct {
    time_t timestamp;
    log_entry_type_t type;
    uint16_t address;
    uint16_t value;
    uint8_t slave_id;
    data_quality_t quality;
    char device_name[64];
    char description[128];
} log_entry_t;

// Logger configuration
typedef struct {
    bool enabled;
    bool log_on_change_only;  // Only log when value changes
    int interval_ms;           // Logging interval in milliseconds
    int max_file_size_mb;      // Maximum file size before rotation
    int retention_days;        // Number of days to keep logs
    char log_directory[256];   // Directory to store log files
    char filename_prefix[64];  // Prefix for log filenames
    bool include_milliseconds; // Include milliseconds in timestamp
    bool compress_old_logs;    // Compress logs older than 1 day
} logger_config_t;

// Tag configuration for continuous logging
typedef struct {
    bool enabled;
    uint16_t address;
    log_entry_type_t type;
    uint8_t slave_id;
    char tag_name[64];
    char description[128];
    uint16_t last_value;
    bool has_last_value;
    int poll_interval_ms;
    time_t last_poll_time;
} log_tag_t;

#define MAX_LOG_TAGS 256

// Alarm severity levels
typedef enum {
    ALARM_SEVERITY_LOW = 0,
    ALARM_SEVERITY_HIGH = 1,
    ALARM_SEVERITY_CRITICAL = 2
} alarm_severity_t;

// Alarm state
typedef enum {
    ALARM_STATE_NORMAL = 0,
    ALARM_STATE_ACTIVE = 1,
    ALARM_STATE_ACKNOWLEDGED = 2
} alarm_state_t;

// Alarm configuration for a register
typedef struct {
    bool enabled;
    uint16_t address;
    log_entry_type_t type;  // Type of register to monitor
    uint8_t slave_id;
    char tag_name[64];
    
    // Thresholds
    float high_high_limit;    // Critical high alarm
    float high_limit;         // High warning
    float low_limit;          // Low warning
    float low_low_limit;      // Critical low alarm
    
    bool high_high_enabled;
    bool high_enabled;
    bool low_enabled;
    bool low_low_enabled;
    
    // Alarm behavior
    int deadband;             // Hysteresis value to prevent flapping
    int poll_interval_ms;     // How often to check this alarm
    time_t last_check_time;
    
    // Current state
    alarm_state_t state;
    alarm_severity_t current_severity;
    uint16_t last_value;
    bool has_last_value;
    time_t alarm_triggered_time;
    
    char alarm_message[128];
} alarm_config_t;

#define MAX_ALARMS 64

// Logger statistics
typedef struct {
    uint64_t total_entries_logged;
    uint64_t total_files_created;
    uint64_t total_bytes_written;
    time_t logger_start_time;
    int active_tags;
    int failed_writes;
} logger_stats_t;

// Function prototypes

// Initialization and configuration
int data_logger_init(const logger_config_t* config);
void data_logger_shutdown(void);
int data_logger_set_config(const logger_config_t* config);
void data_logger_get_config(logger_config_t* config);

// Logging functions
int data_logger_log_entry(const log_entry_t* entry);
int data_logger_log_register(uint16_t address, uint16_t value, uint8_t slave_id, 
                              log_entry_type_t type, data_quality_t quality);
int data_logger_log_event(const char* description, const char* device_name);
int data_logger_log_alarm(uint16_t address, uint16_t value, const char* description, 
                          uint8_t slave_id);

// Tag management for continuous logging
int data_logger_add_tag(const log_tag_t* tag);
int data_logger_remove_tag(int tag_id);
int data_logger_update_tag(int tag_id, const log_tag_t* tag);
int data_logger_get_tag_count(void);
const log_tag_t* data_logger_get_tag(int tag_id);
void data_logger_clear_tags(void);

// Tag polling (call this periodically in main loop)
void data_logger_poll_tags(void* modbus_ctx);

// Alarm management
int data_logger_add_alarm(const alarm_config_t* alarm);
int data_logger_remove_alarm(int alarm_id);
int data_logger_update_alarm(int alarm_id, const alarm_config_t* alarm);
int data_logger_get_alarm_count(void);
const alarm_config_t* data_logger_get_alarm(int alarm_id);
void data_logger_clear_alarms(void);
int data_logger_acknowledge_alarm(int alarm_id);

// Alarm polling (call this periodically in main loop)
void data_logger_poll_alarms(void* modbus_ctx);

// Control functions
void data_logger_enable(bool enable);
bool data_logger_is_enabled(void);
void data_logger_flush(void);
int data_logger_rotate_file(void);

// Maintenance functions
int data_logger_cleanup_old_logs(void);
int data_logger_export_to_csv(const char* output_file, time_t start_time, time_t end_time);
int data_logger_compact_logs(void);

// Statistics and information
void data_logger_get_stats(logger_stats_t* stats);
void data_logger_reset_stats(void);
const char* data_logger_get_current_filename(void);
uint64_t data_logger_get_current_file_size(void);

// Utility functions
const char* data_logger_quality_to_string(data_quality_t quality);
const char* data_logger_type_to_string(log_entry_type_t type);

#endif // DATA_LOGGER_H
