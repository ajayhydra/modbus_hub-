#include "app_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

// Syslog constants
#define EVENT_SOURCE_NAME "ModbusMaster"

// Static variables
static app_logger_config_t s_config = {0};
static bool s_initialized = false;
static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_syslog_opened = false;
static FILE* s_log_file = NULL;
static char s_current_log_path[260] = {0};
static app_logger_stats_t s_stats = {0};

// Level names for logging
static const char* s_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRITICAL"
};

// Syslog priority mapping
static int s_syslog_priorities[] = {
    LOG_DEBUG,      // DEBUG
    LOG_INFO,       // INFO
    LOG_WARNING,    // WARN
    LOG_ERR,        // ERROR
    LOG_CRIT        // CRITICAL
};

// Forward declarations
static void open_syslog(void);
static void close_syslog(void);
static void open_log_file(void);
static void close_log_file(void);
static long get_file_size(const char* filepath);
static void rotate_log_file(void);
static const char* get_timestamp(void);

// Initialize the application logger
int app_logger_init(const app_logger_config_t* config) {
    if (s_initialized) {
        return 0;
    }
    
    if (!config) {
        return -1;
    }
    
    // Copy configuration
    memcpy(&s_config, config, sizeof(app_logger_config_t));
    
    // Initialize statistics
    memset(&s_stats, 0, sizeof(app_logger_stats_t));
    s_stats.start_time = time(NULL);
    
    // Create log directory if it doesn't exist
    if (s_config.log_to_file) {
        mkdir(s_config.log_directory, 0755);
        open_log_file();
    }
    
    // Open syslog
    if (s_config.log_to_syslog) {
        open_syslog();
    }
    
    s_initialized = true;
    
    APP_LOG_INFO("Logger", "Application logger initialized (File: %s, Syslog: %s)",
                 s_config.log_to_file ? "Enabled" : "Disabled",
                 s_config.log_to_syslog ? "Enabled" : "Disabled");
    
    return 0;
}

// Shutdown the logger
void app_logger_shutdown(void) {
    if (!s_initialized) {
        return;
    }
    
    APP_LOG_INFO("Logger", "Application logger shutting down");
    
    close_log_file();
    close_syslog();
    
    pthread_mutex_destroy(&s_log_mutex);
    
    s_initialized = false;
}

// Set minimum log level
void app_logger_set_level(log_level_t level) {
    s_config.min_level = level;
}

// Enable/disable syslog
void app_logger_set_syslog(bool enabled) {
    if (enabled && !s_config.log_to_syslog) {
        open_syslog();
    } else if (!enabled && s_config.log_to_syslog) {
        close_syslog();
    }
    s_config.log_to_syslog = enabled;
}

// Log a message
void app_logger_log(log_level_t level, const char* module, const char* format, ...) {
    if (!s_initialized || !s_config.enabled) {
        return;
    }
    
    if (level < s_config.min_level) {
        return;
    }
    
    pthread_mutex_lock(&s_log_mutex);
    
    // Format the message
    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Format full log entry
    char log_entry[2560];
    snprintf(log_entry, sizeof(log_entry), "%s [%s] [%s] %s\n",
             get_timestamp(),
             s_level_names[level],
             module ? module : "App",
             message);
    
    // Log to file
    if (s_config.log_to_file && s_log_file) {
        fputs(log_entry, s_log_file);
        fflush(s_log_file);
        
        // Check if rotation is needed
        if (s_config.max_file_size_mb > 0) {
            long current_size = ftell(s_log_file);
            if (current_size > (s_config.max_file_size_mb * 1024 * 1024)) {
                rotate_log_file();
            }
        }
    }
    
    // Log to console (for debugging)
    if (s_config.log_to_console) {
        printf("%s", log_entry);
    }
    
    // Log to syslog
    if (s_config.log_to_syslog && s_syslog_opened) {
        syslog(s_syslog_priorities[level], "[%s] %s", module ? module : "App", message);
    }
    
    // Update statistics
    s_stats.total_logs++;
    if (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_CRITICAL) {
        s_stats.errors++;
    } else if (level == LOG_LEVEL_WARN) {
        s_stats.warnings++;
    }
    
    pthread_mutex_unlock(&s_log_mutex);
}

// Log a connection event
void app_logger_log_connection(const char* device, const char* address, bool connected) {
    if (connected) {
        APP_LOG_INFO("Connection", "Connected to %s at %s", device, address);
    } else {
        APP_LOG_INFO("Connection", "Disconnected from %s at %s", device, address);
    }
}

// Log a system error
void app_logger_log_system_error(const char* module, const char* operation, int error_code) {
    char error_msg[512];
    strerror_r(error_code, error_msg, sizeof(error_msg));
    
    APP_LOG_ERROR(module, "%s failed with error %d: %s", operation, error_code, error_msg);
}

// Rotate log file if needed
void app_logger_rotate_if_needed(void) {
    if (!s_config.log_to_file || !s_log_file) {
        return;
    }
    
    pthread_mutex_lock(&s_log_mutex);
    
    long current_size = ftell(s_log_file);
    if (current_size > (s_config.max_file_size_mb * 1024 * 1024)) {
        rotate_log_file();
    }
    
    pthread_mutex_unlock(&s_log_mutex);
}

// Get current log file path
const char* app_logger_get_current_log_file(void) {
    return s_current_log_path;
}

// Get statistics
void app_logger_get_stats(app_logger_stats_t* stats) {
    if (stats) {
        pthread_mutex_lock(&s_log_mutex);
        memcpy(stats, &s_stats, sizeof(app_logger_stats_t));
        strncpy(stats->current_log_file, s_current_log_path, sizeof(stats->current_log_file) - 1);
        pthread_mutex_unlock(&s_log_mutex);
    }
}

// Internal: Open syslog
static void open_syslog(void) {
    openlog(EVENT_SOURCE_NAME, LOG_PID | LOG_CONS, LOG_USER);
    s_syslog_opened = true;
}

// Internal: Close syslog
static void close_syslog(void) {
    if (s_syslog_opened) {
        closelog();
        s_syslog_opened = false;
    }
}

// Internal: Open log file
static void open_log_file(void) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    
    snprintf(s_current_log_path, sizeof(s_current_log_path),
             "%s/%s_%s.log",
             s_config.log_directory,
             s_config.log_filename,
             timestamp);
    
    s_log_file = fopen(s_current_log_path, "a");
    if (!s_log_file) {
        // Fallback to current directory
        snprintf(s_current_log_path, sizeof(s_current_log_path),
                 "%s_%s.log",
                 s_config.log_filename,
                 timestamp);
        s_log_file = fopen(s_current_log_path, "a");
    }
    
    if (s_log_file) {
        strncpy(s_stats.current_log_file, s_current_log_path, sizeof(s_stats.current_log_file) - 1);
        
        // Write header
        fprintf(s_log_file, "=== Modbus Master Application Log ===\n");
        fprintf(s_log_file, "Started: %s\n", get_timestamp());
        fprintf(s_log_file, "======================================\n\n");
        fflush(s_log_file);
    }
}

// Internal: Close log file
static void close_log_file(void) {
    if (s_log_file) {
        fprintf(s_log_file, "\n======================================\n");
        fprintf(s_log_file, "Stopped: %s\n", get_timestamp());
        fprintf(s_log_file, "======================================\n");
        fclose(s_log_file);
        s_log_file = NULL;
    }
}

// Internal: Get file size
static long get_file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// Internal: Rotate log file
static void rotate_log_file(void) {
    close_log_file();
    open_log_file();
    APP_LOG_INFO("Logger", "Log file rotated to: %s", s_current_log_path);
}

// Internal: Get timestamp
static const char* get_timestamp(void) {
    static char timestamp[32];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}
