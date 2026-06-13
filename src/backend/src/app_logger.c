// Cross-platform application logger.
// Writes timestamped, level-tagged log lines to a rolling log file under the
// configured log directory and (optionally) to stdout. The Linux build also
// forwards into syslog when enabled in the config; the Windows build skips
// the system event log for now (Phase 5 work).
#include "app_logger.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_compat(path)  _mkdir(path)
#else
#include <unistd.h>
#include <syslog.h>
#define mkdir_compat(path)  mkdir((path), 0755)
#endif

static app_logger_config_t s_config = {0};
static bool s_initialized = false;
static platform_mutex_t* s_lock = NULL;
static FILE* s_log_file = NULL;
static char s_current_log_path[260] = {0};
static app_logger_stats_t s_stats = {0};
#ifndef _WIN32
static bool s_syslog_opened = false;
#endif

static const char* s_level_names[] = { "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL" };

static void open_log_file(void);
static void close_log_file(void);
static void rotate_log_file(void);
static const char* get_timestamp(void);

#define LOCK()   do { if (s_lock) platform_mutex_lock(s_lock); } while (0)
#define UNLOCK() do { if (s_lock) platform_mutex_unlock(s_lock); } while (0)

int app_logger_init(const app_logger_config_t* config) {
    if (s_initialized) return 0;
    if (!config) return -1;

    if (!s_lock) s_lock = platform_mutex_create();
    memcpy(&s_config, config, sizeof(app_logger_config_t));
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.start_time = time(NULL);

    if (s_config.log_to_file) {
        mkdir_compat(s_config.log_directory);  // best-effort; ignore EEXIST
        open_log_file();
    }

#ifndef _WIN32
    if (s_config.log_to_syslog) {
        openlog("ModbusHub", LOG_PID | LOG_CONS, LOG_USER);
        s_syslog_opened = true;
    }
#endif

    s_initialized = true;
    APP_LOG_INFO("Logger", "Application logger initialized (file=%s, console=%s)",
                 s_config.log_to_file ? "yes" : "no",
                 s_config.log_to_console ? "yes" : "no");
    return 0;
}

void app_logger_shutdown(void) {
    if (!s_initialized) return;
    APP_LOG_INFO("Logger", "Application logger shutting down");

    LOCK();
    close_log_file();
#ifndef _WIN32
    if (s_syslog_opened) { closelog(); s_syslog_opened = false; }
#endif
    s_initialized = false;
    UNLOCK();
}

void app_logger_set_level(log_level_t level) { s_config.min_level = level; }

#ifdef _WIN32
void app_logger_set_event_log(bool enabled) {
    // Windows Event Log integration not implemented in this build.
    s_config.log_to_event_log = enabled;
}
#else
void app_logger_set_syslog(bool enabled) {
    LOCK();
    if (enabled && !s_syslog_opened) {
        openlog("ModbusHub", LOG_PID | LOG_CONS, LOG_USER);
        s_syslog_opened = true;
    } else if (!enabled && s_syslog_opened) {
        closelog();
        s_syslog_opened = false;
    }
    s_config.log_to_syslog = enabled;
    UNLOCK();
}
#endif

void app_logger_log(log_level_t level, const char* module, const char* format, ...) {
    if (!s_initialized || !s_config.enabled) return;
    if (level < s_config.min_level) return;
    if (level < 0 || level > LOG_LEVEL_CRITICAL) return;

    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char log_entry[2560];
    snprintf(log_entry, sizeof(log_entry), "%s [%s] [%s] %s\n",
             get_timestamp(), s_level_names[level], module ? module : "App", message);

    LOCK();
    if (s_config.log_to_file && s_log_file) {
        fputs(log_entry, s_log_file);
        fflush(s_log_file);
        if (s_config.max_file_size_mb > 0) {
            long sz = ftell(s_log_file);
            if (sz > (long)s_config.max_file_size_mb * 1024L * 1024L) {
                rotate_log_file();
            }
        }
    }
    if (s_config.log_to_console) fputs(log_entry, stdout);

#ifndef _WIN32
    if (s_config.log_to_syslog && s_syslog_opened) {
        static const int s_pri[] = { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR, LOG_CRIT };
        syslog(s_pri[level], "[%s] %s", module ? module : "App", message);
    }
#endif

    s_stats.total_logs++;
    if (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_CRITICAL) s_stats.errors++;
    else if (level == LOG_LEVEL_WARN) s_stats.warnings++;
    UNLOCK();
}

void app_logger_log_connection(const char* device, const char* address, bool connected) {
    if (connected) APP_LOG_INFO("Connection", "Connected to %s at %s", device, address);
    else           APP_LOG_INFO("Connection", "Disconnected from %s at %s", device, address);
}

void app_logger_log_system_error(const char* module, const char* operation, int error_code) {
    // strerror_r has incompatible signatures across platforms (POSIX vs GNU);
    // strerror is fine here since we hold the logger mutex during formatting.
    APP_LOG_ERROR(module, "%s failed with error %d: %s",
                  operation, error_code, strerror(error_code));
}

void app_logger_rotate_if_needed(void) {
    if (!s_config.log_to_file || !s_log_file) return;
    LOCK();
    long sz = ftell(s_log_file);
    if (sz > (long)s_config.max_file_size_mb * 1024L * 1024L) rotate_log_file();
    UNLOCK();
}

const char* app_logger_get_current_log_file(void) { return s_current_log_path; }

void app_logger_get_stats(app_logger_stats_t* stats) {
    if (!stats) return;
    LOCK();
    memcpy(stats, &s_stats, sizeof(*stats));
    strncpy(stats->current_log_file, s_current_log_path, sizeof(stats->current_log_file) - 1);
    stats->current_log_file[sizeof(stats->current_log_file) - 1] = '\0';
    UNLOCK();
}

// ── Internals ────────────────────────────────────────────────────────────
static void open_log_file(void) {
    time_t now = time(NULL);
    struct tm* tmi = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tmi);

    snprintf(s_current_log_path, sizeof(s_current_log_path),
             "%s/%s_%s.log", s_config.log_directory, s_config.log_filename, ts);

    s_log_file = fopen(s_current_log_path, "a");
    if (!s_log_file) {
        // Fallback: try the working directory
        snprintf(s_current_log_path, sizeof(s_current_log_path),
                 "%s_%s.log", s_config.log_filename, ts);
        s_log_file = fopen(s_current_log_path, "a");
    }

    if (s_log_file) {
        strncpy(s_stats.current_log_file, s_current_log_path,
                sizeof(s_stats.current_log_file) - 1);
        fprintf(s_log_file, "=== ModbusHub Application Log ===\n");
        fprintf(s_log_file, "Started: %s\n\n", get_timestamp());
        fflush(s_log_file);
    }
}

static void close_log_file(void) {
    if (!s_log_file) return;
    fprintf(s_log_file, "\nStopped: %s\n", get_timestamp());
    fclose(s_log_file);
    s_log_file = NULL;
}

static void rotate_log_file(void) {
    close_log_file();
    open_log_file();
}

static const char* get_timestamp(void) {
    static char buf[32];
    time_t now = time(NULL);
    struct tm* tmi = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmi);
    return buf;
}
