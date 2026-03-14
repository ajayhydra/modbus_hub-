#include "data_logger.h"
#include "modbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <gtk/gtk.h>

// Global logger state
static struct {
    logger_config_t config;
    FILE* current_file;
    char current_filename[512];
    logger_stats_t stats;
    log_tag_t tags[MAX_LOG_TAGS];
    int tag_count;
    alarm_config_t alarms[MAX_ALARMS];
    int alarm_count;
    pthread_mutex_t lock;
    bool initialized;
} g_logger = {0};

// Forward declarations
static int create_log_directory(const char* path);
static int open_new_log_file(void);
static void close_current_log_file(void);
static char* get_timestamp_string(time_t timestamp, bool include_ms, char* buffer, size_t buffer_size);
static int write_csv_header(FILE* file);
static int should_rotate_file(void);
static void show_gtk_notification(const char* title, const char* message, const char* icon);

// Initialize the data logger
int data_logger_init(const logger_config_t* config) {
    if (g_logger.initialized) {
        return 0; // Already initialized
    }

    // Initialize mutex for thread safety
    if (pthread_mutex_init(&g_logger.lock, NULL) != 0) {
        return -1;
    }

    // Set default configuration
    if (config) {
        memcpy(&g_logger.config, config, sizeof(logger_config_t));
    } else {
        // Default configuration
        g_logger.config.enabled = true;
        g_logger.config.log_on_change_only = false;
        g_logger.config.interval_ms = 1000;
        g_logger.config.max_file_size_mb = 100;
        g_logger.config.retention_days = 30;
        strcpy(g_logger.config.log_directory, "logs");
        strcpy(g_logger.config.filename_prefix, "modbus_data");
        g_logger.config.include_milliseconds = true;
        g_logger.config.compress_old_logs = false;
    }

    // Create log directory if it doesn't exist
    if (create_log_directory(g_logger.config.log_directory) != 0) {
        pthread_mutex_destroy(&g_logger.lock);
        return -1;
    }

    // Initialize statistics
    memset(&g_logger.stats, 0, sizeof(logger_stats_t));
    g_logger.stats.logger_start_time = time(NULL);

    // Initialize tags
    g_logger.tag_count = 0;
    memset(g_logger.tags, 0, sizeof(g_logger.tags));
    
    // Initialize alarms
    g_logger.alarm_count = 0;
    memset(g_logger.alarms, 0, sizeof(g_logger.alarms));

    // Open initial log file
    if (g_logger.config.enabled) {
        if (open_new_log_file() != 0) {
            pthread_mutex_destroy(&g_logger.lock);
            return -1;
        }
    }

    g_logger.initialized = true;
    return 0;
}

// Shutdown the data logger
void data_logger_shutdown(void) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    close_current_log_file();
    g_logger.initialized = false;
    
    pthread_mutex_unlock(&g_logger.lock);
    pthread_mutex_destroy(&g_logger.lock);
}

// Set logger configuration
int data_logger_set_config(const logger_config_t* config) {
    if (!g_logger.initialized || !config) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    bool was_enabled = g_logger.config.enabled;
    memcpy(&g_logger.config, config, sizeof(logger_config_t));

    // If log directory changed, create it
    create_log_directory(g_logger.config.log_directory);

    // If enabling or directory changed, open new file
    if (g_logger.config.enabled && (!was_enabled || g_logger.current_file == NULL)) {
        open_new_log_file();
    } else if (!g_logger.config.enabled && was_enabled) {
        close_current_log_file();
    }

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Get current configuration
void data_logger_get_config(logger_config_t* config) {
    if (!g_logger.initialized || !config) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    memcpy(config, &g_logger.config, sizeof(logger_config_t));
    pthread_mutex_unlock(&g_logger.lock);
}

// Log a complete entry
int data_logger_log_entry(const log_entry_t* entry) {
    if (!g_logger.initialized || !g_logger.config.enabled || !entry) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);

    // Check if file rotation is needed
    if (should_rotate_file()) {
        data_logger_rotate_file();
    }

    if (!g_logger.current_file) {
        pthread_mutex_unlock(&g_logger.lock);
        return -1;
    }

    // Format timestamp
    char timestamp_str[64];
    get_timestamp_string(entry->timestamp, g_logger.config.include_milliseconds, 
                        timestamp_str, sizeof(timestamp_str));

    // Write CSV line
    fprintf(g_logger.current_file, "%s,%s,%u,%u,%u,%s,%s,%s\n",
            timestamp_str,
            data_logger_type_to_string(entry->type),
            entry->slave_id,
            entry->address,
            entry->value,
            data_logger_quality_to_string(entry->quality),
            entry->device_name,
            entry->description);

    // Update statistics
    g_logger.stats.total_entries_logged++;
    
    // Flush to disk for critical data
    if (entry->quality != QUALITY_GOOD) {
        fflush(g_logger.current_file);
    }

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Log a register value
int data_logger_log_register(uint16_t address, uint16_t value, uint8_t slave_id,
                              log_entry_type_t type, data_quality_t quality) {
    log_entry_t entry = {0};
    entry.timestamp = time(NULL);
    entry.type = type;
    entry.address = address;
    entry.value = value;
    entry.slave_id = slave_id;
    entry.quality = quality;
    snprintf(entry.device_name, sizeof(entry.device_name), "Device_%u", slave_id);
    snprintf(entry.description, sizeof(entry.description), "Register %u", address);

    return data_logger_log_entry(&entry);
}

// Log an event
int data_logger_log_event(const char* description, const char* device_name) {
    log_entry_t entry = {0};
    entry.timestamp = time(NULL);
    entry.type = LOG_TYPE_EVENT;
    entry.quality = QUALITY_GOOD;
    strncpy(entry.description, description, sizeof(entry.description) - 1);
    strncpy(entry.device_name, device_name ? device_name : "System", sizeof(entry.device_name) - 1);

    return data_logger_log_entry(&entry);
}

// Log an alarm
int data_logger_log_alarm(uint16_t address, uint16_t value, const char* description,
                          uint8_t slave_id) {
    log_entry_t entry = {0};
    entry.timestamp = time(NULL);
    entry.type = LOG_TYPE_ALARM;
    entry.address = address;
    entry.value = value;
    entry.slave_id = slave_id;
    entry.quality = QUALITY_GOOD;
    snprintf(entry.device_name, sizeof(entry.device_name), "Device_%u", slave_id);
    strncpy(entry.description, description, sizeof(entry.description) - 1);

    return data_logger_log_entry(&entry);
}

// Add a tag for continuous logging
int data_logger_add_tag(const log_tag_t* tag) {
    if (!g_logger.initialized || !tag || g_logger.tag_count >= MAX_LOG_TAGS) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    int tag_id = g_logger.tag_count;
    memcpy(&g_logger.tags[tag_id], tag, sizeof(log_tag_t));
    g_logger.tags[tag_id].last_poll_time = 0;
    g_logger.tags[tag_id].has_last_value = false;
    g_logger.tag_count++;
    g_logger.stats.active_tags = g_logger.tag_count;

    pthread_mutex_unlock(&g_logger.lock);
    return tag_id;
}

// Remove a tag
int data_logger_remove_tag(int tag_id) {
    if (!g_logger.initialized || tag_id < 0 || tag_id >= g_logger.tag_count) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    // Shift all tags down
    for (int i = tag_id; i < g_logger.tag_count - 1; i++) {
        memcpy(&g_logger.tags[i], &g_logger.tags[i + 1], sizeof(log_tag_t));
    }
    g_logger.tag_count--;
    g_logger.stats.active_tags = g_logger.tag_count;

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Update a tag
int data_logger_update_tag(int tag_id, const log_tag_t* tag) {
    if (!g_logger.initialized || !tag || tag_id < 0 || tag_id >= g_logger.tag_count) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    // Preserve polling state
    time_t last_poll = g_logger.tags[tag_id].last_poll_time;
    bool has_value = g_logger.tags[tag_id].has_last_value;
    uint16_t last_value = g_logger.tags[tag_id].last_value;
    
    memcpy(&g_logger.tags[tag_id], tag, sizeof(log_tag_t));
    
    g_logger.tags[tag_id].last_poll_time = last_poll;
    g_logger.tags[tag_id].has_last_value = has_value;
    g_logger.tags[tag_id].last_value = last_value;

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Get tag count
int data_logger_get_tag_count(void) {
    if (!g_logger.initialized) {
        return 0;
    }
    return g_logger.tag_count;
}

// Get tag by ID
const log_tag_t* data_logger_get_tag(int tag_id) {
    if (!g_logger.initialized || tag_id < 0 || tag_id >= g_logger.tag_count) {
        return NULL;
    }
    return &g_logger.tags[tag_id];
}

// Clear all tags
void data_logger_clear_tags(void) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    g_logger.tag_count = 0;
    memset(g_logger.tags, 0, sizeof(g_logger.tags));
    g_logger.stats.active_tags = 0;
    pthread_mutex_unlock(&g_logger.lock);
}

// Poll tags and log values
void data_logger_poll_tags(void* modbus_ctx) {
    if (!g_logger.initialized || !modbus_ctx || !g_logger.config.enabled) {
        return;
    }

    modbus_t* ctx = (modbus_t*)modbus_ctx;
    time_t current_time = time(NULL);

    pthread_mutex_lock(&g_logger.lock);

    for (int i = 0; i < g_logger.tag_count; i++) {
        log_tag_t* tag = &g_logger.tags[i];

        if (!tag->enabled) {
            continue;
        }

        // Check if it's time to poll
        if (tag->poll_interval_ms > 0) {
            if (tag->last_poll_time > 0) {
                time_t elapsed_ms = (current_time - tag->last_poll_time) * 1000;
                if (elapsed_ms < tag->poll_interval_ms) {
                    continue;
                }
            }
        }

        tag->last_poll_time = current_time;

        // Set slave ID
        modbus_set_slave(ctx, tag->slave_id);

        // Read register
        uint16_t value = 0;
        int rc = -1;
        data_quality_t quality = QUALITY_GOOD;

        if (tag->type == LOG_TYPE_HOLDING_REGISTER) {
            rc = modbus_read_registers(ctx, tag->address, 1, &value);
        } else if (tag->type == LOG_TYPE_INPUT_REGISTER) {
            rc = modbus_read_input_registers(ctx, tag->address, 1, &value);
        } else if (tag->type == LOG_TYPE_COIL) {
            uint8_t bit_value;
            rc = modbus_read_bits(ctx, tag->address, 1, &bit_value);
            value = bit_value;
        } else if (tag->type == LOG_TYPE_DISCRETE_INPUT) {
            uint8_t bit_value;
            rc = modbus_read_input_bits(ctx, tag->address, 1, &bit_value);
            value = bit_value;
        }

        if (rc < 0) {
            quality = QUALITY_COMM_ERROR;
        }

        // Check if we should log
        bool should_log = (quality != QUALITY_GOOD) || 
                         !g_logger.config.log_on_change_only ||
                         !tag->has_last_value ||
                         (tag->last_value != value);

        if (should_log) {
            log_entry_t entry = {0};
            entry.timestamp = current_time;
            entry.type = tag->type;
            entry.address = tag->address;
            entry.value = value;
            entry.slave_id = tag->slave_id;
            entry.quality = quality;
            strncpy(entry.device_name, tag->tag_name, sizeof(entry.device_name) - 1);
            strncpy(entry.description, tag->description, sizeof(entry.description) - 1);

            pthread_mutex_unlock(&g_logger.lock);
            data_logger_log_entry(&entry);
            pthread_mutex_lock(&g_logger.lock);

            tag->last_value = value;
            tag->has_last_value = true;
        }
    }

    pthread_mutex_unlock(&g_logger.lock);
}

// Add an alarm configuration
int data_logger_add_alarm(const alarm_config_t* alarm) {
    if (!g_logger.initialized || !alarm || g_logger.alarm_count >= MAX_ALARMS) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);

    memcpy(&g_logger.alarms[g_logger.alarm_count], alarm, sizeof(alarm_config_t));
    g_logger.alarms[g_logger.alarm_count].state = ALARM_STATE_NORMAL;
    g_logger.alarms[g_logger.alarm_count].has_last_value = false;
    g_logger.alarms[g_logger.alarm_count].last_check_time = 0;

    int alarm_id = g_logger.alarm_count;
    g_logger.alarm_count++;

    pthread_mutex_unlock(&g_logger.lock);
    return alarm_id;
}

// Remove an alarm
int data_logger_remove_alarm(int alarm_id) {
    if (!g_logger.initialized || alarm_id < 0 || alarm_id >= g_logger.alarm_count) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);

    for (int i = alarm_id; i < g_logger.alarm_count - 1; i++) {
        memcpy(&g_logger.alarms[i], &g_logger.alarms[i + 1], sizeof(alarm_config_t));
    }

    g_logger.alarm_count--;

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Update an alarm configuration
int data_logger_update_alarm(int alarm_id, const alarm_config_t* alarm) {
    if (!g_logger.initialized || !alarm || alarm_id < 0 || alarm_id >= g_logger.alarm_count) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);

    alarm_state_t old_state = g_logger.alarms[alarm_id].state;
    bool old_has_value = g_logger.alarms[alarm_id].has_last_value;
    uint16_t old_value = g_logger.alarms[alarm_id].last_value;

    memcpy(&g_logger.alarms[alarm_id], alarm, sizeof(alarm_config_t));

    g_logger.alarms[alarm_id].state = old_state;
    g_logger.alarms[alarm_id].has_last_value = old_has_value;
    g_logger.alarms[alarm_id].last_value = old_value;

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Get alarm count
int data_logger_get_alarm_count(void) {
    if (!g_logger.initialized) {
        return 0;
    }
    return g_logger.alarm_count;
}

// Get alarm by ID
const alarm_config_t* data_logger_get_alarm(int alarm_id) {
    if (!g_logger.initialized || alarm_id < 0 || alarm_id >= g_logger.alarm_count) {
        return NULL;
    }
    return &g_logger.alarms[alarm_id];
}

// Clear all alarms
void data_logger_clear_alarms(void) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    g_logger.alarm_count = 0;
    memset(g_logger.alarms, 0, sizeof(g_logger.alarms));
    pthread_mutex_unlock(&g_logger.lock);
}

// Acknowledge an alarm
int data_logger_acknowledge_alarm(int alarm_id) {
    if (!g_logger.initialized || alarm_id < 0 || alarm_id >= g_logger.alarm_count) {
        return -1;
    }

    pthread_mutex_lock(&g_logger.lock);

    if (g_logger.alarms[alarm_id].state == ALARM_STATE_ACTIVE) {
        g_logger.alarms[alarm_id].state = ALARM_STATE_ACKNOWLEDGED;
        data_logger_log_event("Alarm acknowledged", g_logger.alarms[alarm_id].tag_name);
    }

    pthread_mutex_unlock(&g_logger.lock);
    return 0;
}

// Poll alarms and check thresholds
void data_logger_poll_alarms(void* modbus_ctx) {
    if (!g_logger.initialized || !modbus_ctx || !g_logger.config.enabled) {
        return;
    }

    modbus_t* ctx = (modbus_t*)modbus_ctx;
    time_t current_time = time(NULL);

    pthread_mutex_lock(&g_logger.lock);

    for (int i = 0; i < g_logger.alarm_count; i++) {
        alarm_config_t* alarm = &g_logger.alarms[i];

        if (!alarm->enabled) {
            continue;
        }

        // Check if it's time to poll this alarm
        if (alarm->poll_interval_ms > 0) {
            if (alarm->last_check_time > 0) {
                time_t elapsed_ms = (current_time - alarm->last_check_time) * 1000;
                if (elapsed_ms < alarm->poll_interval_ms) {
                    continue;
                }
            }
        }

        alarm->last_check_time = current_time;

        modbus_set_slave(ctx, alarm->slave_id);

        uint16_t value;
        int result = -1;

        if (alarm->type == LOG_TYPE_HOLDING_REGISTER) {
            result = modbus_read_registers(ctx, alarm->address, 1, &value);
        } else if (alarm->type == LOG_TYPE_INPUT_REGISTER) {
            result = modbus_read_input_registers(ctx, alarm->address, 1, &value);
        }

        if (result <= 0) {
            continue;
        }

        bool alarm_triggered = false;
        alarm_severity_t severity = ALARM_SEVERITY_LOW;
        char alarm_msg[256];
        bool had_prev_value = alarm->has_last_value;

        alarm->last_value = value;
        alarm->has_last_value = true;

        // Check critical high
        if (alarm->high_high_enabled && value >= alarm->high_high_limit) {
            if (!had_prev_value || alarm->state == ALARM_STATE_NORMAL) {
                alarm_triggered = true;
                severity = ALARM_SEVERITY_CRITICAL;
                snprintf(alarm_msg, sizeof(alarm_msg), "CRITICAL HIGH: %s = %u (Limit: %.0f) - %s",
                         alarm->tag_name, value, alarm->high_high_limit, alarm->alarm_message);
            }
        }
        // Check high
        else if (alarm->high_enabled && value >= alarm->high_limit) {
            if (!had_prev_value || alarm->state == ALARM_STATE_NORMAL) {
                alarm_triggered = true;
                severity = ALARM_SEVERITY_HIGH;
                snprintf(alarm_msg, sizeof(alarm_msg), "HIGH: %s = %u (Limit: %.0f) - %s",
                         alarm->tag_name, value, alarm->high_limit, alarm->alarm_message);
            }
        }
        // Check low
        else if (alarm->low_enabled && value <= alarm->low_limit) {
            if (!had_prev_value || alarm->state == ALARM_STATE_NORMAL) {
                alarm_triggered = true;
                severity = ALARM_SEVERITY_HIGH;
                snprintf(alarm_msg, sizeof(alarm_msg), "LOW: %s = %u (Limit: %.0f) - %s",
                         alarm->tag_name, value, alarm->low_limit, alarm->alarm_message);
            }
        }
        // Check critical low
        else if (alarm->low_low_enabled && value <= alarm->low_low_limit) {
            if (!had_prev_value || alarm->state == ALARM_STATE_NORMAL) {
                alarm_triggered = true;
                severity = ALARM_SEVERITY_CRITICAL;
                snprintf(alarm_msg, sizeof(alarm_msg), "CRITICAL LOW: %s = %u (Limit: %.0f) - %s",
                         alarm->tag_name, value, alarm->low_low_limit, alarm->alarm_message);
            }
        }
        // Value returned to normal
        else {
            if (alarm->state != ALARM_STATE_NORMAL) {
                snprintf(alarm_msg, sizeof(alarm_msg), "NORMAL: %s = %u - Alarm cleared",
                         alarm->tag_name, value);
                data_logger_log_alarm(alarm->address, value, alarm_msg, alarm->slave_id);
                alarm->state = ALARM_STATE_NORMAL;
            }
        }

        // Log alarm if triggered
        if (alarm_triggered) {
            alarm->state = ALARM_STATE_ACTIVE;
            alarm->current_severity = severity;
            alarm->alarm_triggered_time = current_time;
            data_logger_log_alarm(alarm->address, value, alarm_msg, alarm->slave_id);
            
            // Show GTK notification
            const char* severity_str = (severity == ALARM_SEVERITY_CRITICAL) ? "CRITICAL ALARM" : 
                                       (severity == ALARM_SEVERITY_HIGH) ? "HIGH ALARM" : "LOW ALARM";
            const char* icon = (severity == ALARM_SEVERITY_CRITICAL) ? "dialog-error" : "dialog-warning";
            show_gtk_notification(severity_str, alarm_msg, icon);
        }
    }

    pthread_mutex_unlock(&g_logger.lock);
}

// Enable/disable logger
void data_logger_enable(bool enable) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    
    if (enable && !g_logger.config.enabled) {
        g_logger.config.enabled = true;
        if (!g_logger.current_file) {
            open_new_log_file();
        }
    } else if (!enable && g_logger.config.enabled) {
        g_logger.config.enabled = false;
        close_current_log_file();
    }

    pthread_mutex_unlock(&g_logger.lock);
}

// Check if logger is enabled
bool data_logger_is_enabled(void) {
    return g_logger.initialized && g_logger.config.enabled;
}

// Flush current log file
void data_logger_flush(void) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    if (g_logger.current_file) {
        fflush(g_logger.current_file);
    }
    pthread_mutex_unlock(&g_logger.lock);
}

// Rotate log file
int data_logger_rotate_file(void) {
    if (!g_logger.initialized) {
        return -1;
    }

    close_current_log_file();
    return open_new_log_file();
}

// Cleanup old logs based on retention policy
int data_logger_cleanup_old_logs(void) {
    if (!g_logger.initialized) {
        return -1;
    }

    DIR* dir;
    struct dirent* entry;
    struct stat file_stat;
    time_t current_time = time(NULL);
    time_t cutoff_time = current_time - (g_logger.config.retention_days * 86400);
    int deleted_count = 0;

    dir = opendir(g_logger.config.log_directory);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Check if filename matches our prefix
        if (strncmp(entry->d_name, g_logger.config.filename_prefix, 
                   strlen(g_logger.config.filename_prefix)) != 0) {
            continue;
        }

        // Build full path
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", 
                g_logger.config.log_directory, entry->d_name);

        // Get file stats
        if (stat(file_path, &file_stat) == 0) {
            if (S_ISREG(file_stat.st_mode) && file_stat.st_mtime < cutoff_time) {
                if (unlink(file_path) == 0) {
                    deleted_count++;
                }
            }
        }
    }

    closedir(dir);
    return deleted_count;
}

// Get statistics
void data_logger_get_stats(logger_stats_t* stats) {
    if (!g_logger.initialized || !stats) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    memcpy(stats, &g_logger.stats, sizeof(logger_stats_t));
    pthread_mutex_unlock(&g_logger.lock);
}

// Reset statistics
void data_logger_reset_stats(void) {
    if (!g_logger.initialized) {
        return;
    }

    pthread_mutex_lock(&g_logger.lock);
    g_logger.stats.total_entries_logged = 0;
    g_logger.stats.failed_writes = 0;
    g_logger.stats.logger_start_time = time(NULL);
    pthread_mutex_unlock(&g_logger.lock);
}

// Get current filename
const char* data_logger_get_current_filename(void) {
    return g_logger.current_filename;
}

// Get current file size
uint64_t data_logger_get_current_file_size(void) {
    if (!g_logger.initialized || !g_logger.current_file) {
        return 0;
    }

    pthread_mutex_lock(&g_logger.lock);
    long pos = ftell(g_logger.current_file);
    pthread_mutex_unlock(&g_logger.lock);
    
    return (pos >= 0) ? (uint64_t)pos : 0;
}

// Utility: Quality to string
const char* data_logger_quality_to_string(data_quality_t quality) {
    switch (quality) {
        case QUALITY_GOOD: return "GOOD";
        case QUALITY_BAD: return "BAD";
        case QUALITY_UNCERTAIN: return "UNCERTAIN";
        case QUALITY_COMM_ERROR: return "COMM_ERROR";
        default: return "UNKNOWN";
    }
}

// Utility: Type to string
const char* data_logger_type_to_string(log_entry_type_t type) {
    switch (type) {
        case LOG_TYPE_COIL: return "COIL";
        case LOG_TYPE_DISCRETE_INPUT: return "DISCRETE_INPUT";
        case LOG_TYPE_HOLDING_REGISTER: return "HOLDING_REGISTER";
        case LOG_TYPE_INPUT_REGISTER: return "INPUT_REGISTER";
        case LOG_TYPE_EVENT: return "EVENT";
        case LOG_TYPE_ALARM: return "ALARM";
        default: return "UNKNOWN";
    }
}

// Stub implementations for optional functions
int data_logger_export_to_csv(const char* output_file, time_t start_time, time_t end_time) {
    // Not implemented in this version
    return -1;
}

int data_logger_compact_logs(void) {
    // Not implemented in this version
    return -1;
}

// Private: Create log directory
static int create_log_directory(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        return mkdir(path, 0755);
    }
    return 0;
}

// Private: Open new log file
static int open_new_log_file(void) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    snprintf(g_logger.current_filename, sizeof(g_logger.current_filename),
             "%s/%s_%04d%02d%02d_%02d%02d%02d.csv",
             g_logger.config.log_directory,
             g_logger.config.filename_prefix,
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);

    g_logger.current_file = fopen(g_logger.current_filename, "a");
    if (!g_logger.current_file) {
        return -1;
    }

    // Write header if new file
    fseek(g_logger.current_file, 0, SEEK_END);
    if (ftell(g_logger.current_file) == 0) {
        write_csv_header(g_logger.current_file);
    }

    g_logger.stats.total_files_created++;
    return 0;
}

// Private: Close current log file
static void close_current_log_file(void) {
    if (g_logger.current_file) {
        fflush(g_logger.current_file);
        fclose(g_logger.current_file);
        g_logger.current_file = NULL;
    }
}

// Private: Get timestamp string
static char* get_timestamp_string(time_t timestamp, bool include_ms, char* buffer, size_t buffer_size) {
    struct tm* tm_info = localtime(&timestamp);
    
    if (include_ms) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tv.tv_usec / 1000);
    } else {
        snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    
    return buffer;
}

// Private: Write CSV header
static int write_csv_header(FILE* file) {
    fprintf(file, "Timestamp,Type,SlaveID,Address,Value,Quality,Device,Description\n");
    return 0;
}

// Private: Check if file rotation is needed
static int should_rotate_file(void) {
    if (!g_logger.current_file) {
        return 1;
    }

    long size = ftell(g_logger.current_file);
    long max_size = (long)g_logger.config.max_file_size_mb * 1024 * 1024;
    
    return (size >= max_size);
}

// Private: Show GTK notification
static void show_gtk_notification(const char* title, const char* message, const char* icon) {
    // This function uses GTK notifications (requires GTK runtime)
    // Note: In practice, this may need to be called from the GTK main thread
    GNotification* notification = g_notification_new(title);
    g_notification_set_body(notification, message);
    
    // Set icon if available
    if (icon) {
        GIcon* gicon = g_themed_icon_new(icon);
        g_notification_set_icon(notification, gicon);
        g_object_unref(gicon);
    }
    
    // Note: This requires a GApplication instance to be properly initialized
    // For a complete implementation, you'd need:
    // g_application_send_notification(app, "alarm-notification", notification);
    
    g_object_unref(notification);
}
