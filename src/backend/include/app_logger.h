#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <stdbool.h>
#include <time.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_CRITICAL = 4
} log_level_t;

// Log configuration
typedef struct {
    bool enabled;
    bool log_to_file;
#ifdef _WIN32
    bool log_to_event_log;    // Windows Event Log
#else
    bool log_to_syslog;       // Linux syslog
#endif
    bool log_to_console;
    log_level_t min_level;
    char log_directory[260];
    char log_filename[64];
    int max_file_size_mb;
    bool rotate_on_startup;
} app_logger_config_t;

// Initialize the application logger
// Call this once at application startup
// Returns: 0 on success, -1 on error
int app_logger_init(const app_logger_config_t* config);

// Shutdown the logger and cleanup resources
void app_logger_shutdown(void);

// Set minimum log level (logs below this level will be ignored)
void app_logger_set_level(log_level_t level);

// Enable/disable syslog/event log integration
#ifdef _WIN32
void app_logger_set_event_log(bool enabled);
#define app_logger_set_syslog app_logger_set_event_log
#else
void app_logger_set_syslog(bool enabled);
#endif

// Log a message with specified level
void app_logger_log(log_level_t level, const char* module, const char* format, ...);

// Convenience macros for different log levels
#define APP_LOG_DEBUG(module, ...) app_logger_log(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define APP_LOG_INFO(module, ...) app_logger_log(LOG_LEVEL_INFO, module, __VA_ARGS__)
#define APP_LOG_WARN(module, ...) app_logger_log(LOG_LEVEL_WARN, module, __VA_ARGS__)
#define APP_LOG_ERROR(module, ...) app_logger_log(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#define APP_LOG_CRITICAL(module, ...) app_logger_log(LOG_LEVEL_CRITICAL, module, __VA_ARGS__)

// Log a connection event
void app_logger_log_connection(const char* device, const char* address, bool connected);

// Log a system error (errno on Linux, GetLastError on Windows)
void app_logger_log_system_error(const char* module, const char* operation, int error_code);

// Rotate log file if it exceeds size limit
void app_logger_rotate_if_needed(void);

// Get the full path to the current log file
const char* app_logger_get_current_log_file(void);

// Get log statistics
typedef struct {
    unsigned long total_logs;
    unsigned long errors;
    unsigned long warnings;
    time_t start_time;
    char current_log_file[260];
} app_logger_stats_t;

void app_logger_get_stats(app_logger_stats_t* stats);

#endif // APP_LOGGER_H
