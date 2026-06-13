// Cross-platform CSV data logger for ModbusHub.
//
// Replaces the previous GTK + dirent implementation with a minimal,
// MinGW-friendly logger focused on the public API in data_logger.h:
//
//   * Industrial-grade CSV output with timestamps and quality flags
//   * Size-based file rotation
//   * Append-friendly: log_register, log_event, log_alarm
//   * Statistics (entries, files, bytes)
//
// Tag/alarm management functions are kept as harmless stubs — the Qt UI
// schedules its own polling (QTimer) and alarm logic lives in
// alarm_manager.c. cleanup_old_logs / export / compact are similarly
// stubbed; they are listed in Phase 5 (industrial polish) for full
// implementation.
#include "data_logger.h"
#include "platform.h"
#include "app_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_compat(p)  _mkdir(p)
#else
#include <unistd.h>
#define mkdir_compat(p)  mkdir((p), 0755)
#endif

static struct {
    logger_config_t config;
    FILE* file;
    char filename[512];
    logger_stats_t stats;
    log_tag_t tags[MAX_LOG_TAGS];          // declared but unused — see header note
    int tag_count;
    alarm_config_t alarms[MAX_ALARMS];     // ditto
    int alarm_count;
    platform_mutex_t* lock;
    bool initialized;
    bool enabled;
} g_logger = {0};

// ── Forward declarations ─────────────────────────────────────────────────
static int  open_new_log_file(void);
static void close_log_file(void);
static int  write_csv_header(FILE* fp);
static bool should_rotate(void);
static void rotate_if_needed(void);
static void format_timestamp(time_t t, bool include_ms, char* buf, size_t n);

#define LOCK()   do { if (g_logger.lock) platform_mutex_lock(g_logger.lock); } while (0)
#define UNLOCK() do { if (g_logger.lock) platform_mutex_unlock(g_logger.lock); } while (0)

// ── Lifecycle ────────────────────────────────────────────────────────────
int data_logger_init(const logger_config_t* config) {
    if (g_logger.initialized) return 0;

    if (!g_logger.lock) g_logger.lock = platform_mutex_create();

    if (config) {
        memcpy(&g_logger.config, config, sizeof(logger_config_t));
    } else {
        g_logger.config.enabled = true;
        g_logger.config.log_on_change_only = false;
        g_logger.config.interval_ms = 1000;
        g_logger.config.max_file_size_mb = 100;
        g_logger.config.retention_days = 90;
        strncpy(g_logger.config.log_directory, "logs",
                sizeof(g_logger.config.log_directory) - 1);
        strncpy(g_logger.config.filename_prefix, "modbus_data",
                sizeof(g_logger.config.filename_prefix) - 1);
        g_logger.config.include_milliseconds = true;
        g_logger.config.compress_old_logs = false;
    }

    mkdir_compat(g_logger.config.log_directory);  // best-effort

    memset(&g_logger.stats, 0, sizeof(g_logger.stats));
    g_logger.stats.logger_start_time = time(NULL);
    g_logger.tag_count = 0;
    g_logger.alarm_count = 0;
    g_logger.enabled = g_logger.config.enabled;
    g_logger.initialized = true;

    if (g_logger.enabled) open_new_log_file();
    return 0;
}

void data_logger_shutdown(void) {
    if (!g_logger.initialized) return;
    LOCK();
    close_log_file();
    g_logger.initialized = false;
    UNLOCK();
}

int data_logger_set_config(const logger_config_t* config) {
    if (!config) return -1;
    LOCK();
    memcpy(&g_logger.config, config, sizeof(logger_config_t));
    g_logger.enabled = g_logger.config.enabled;
    UNLOCK();
    return 0;
}

void data_logger_get_config(logger_config_t* config) {
    if (!config) return;
    LOCK();
    memcpy(config, &g_logger.config, sizeof(logger_config_t));
    UNLOCK();
}

void data_logger_enable(bool enable) {
    LOCK();
    g_logger.enabled = enable;
    if (enable && !g_logger.file) open_new_log_file();
    UNLOCK();
}

bool data_logger_is_enabled(void) {
    bool e;
    LOCK(); e = g_logger.enabled; UNLOCK();
    return e;
}

void data_logger_flush(void) {
    LOCK();
    if (g_logger.file) fflush(g_logger.file);
    UNLOCK();
}

int data_logger_rotate_file(void) {
    LOCK();
    close_log_file();
    int rc = open_new_log_file();
    UNLOCK();
    return rc;
}

// ── Logging ──────────────────────────────────────────────────────────────
int data_logger_log_entry(const log_entry_t* entry) {
    if (!entry) return -1;
    if (!g_logger.initialized || !g_logger.enabled) return 0;

    char ts[40];
    format_timestamp(entry->timestamp ? entry->timestamp : time(NULL),
                     g_logger.config.include_milliseconds, ts, sizeof(ts));

    LOCK();
    if (!g_logger.file) {
        UNLOCK();
        return -1;
    }
    int written = fprintf(g_logger.file,
        "%s,%s,%u,%u,%u,%s,%s,%s\n",
        ts,
        data_logger_type_to_string(entry->type),
        (unsigned)entry->slave_id,
        (unsigned)entry->address,
        (unsigned)entry->value,
        data_logger_quality_to_string(entry->quality),
        entry->device_name[0] ? entry->device_name : "ModbusHub",
        entry->description[0] ? entry->description : "");
    fflush(g_logger.file);

    if (written > 0) {
        g_logger.stats.total_entries_logged++;
        g_logger.stats.total_bytes_written += (uint64_t)written;
    } else {
        g_logger.stats.failed_writes++;
    }
    rotate_if_needed();
    UNLOCK();
    return written > 0 ? 0 : -1;
}

int data_logger_log_register(uint16_t address, uint16_t value, uint8_t slave_id,
                             log_entry_type_t type, data_quality_t quality) {
    log_entry_t e = {0};
    e.timestamp = time(NULL);
    e.type = type;
    e.address = address;
    e.value = value;
    e.slave_id = slave_id;
    e.quality = quality;
    snprintf(e.description, sizeof(e.description), "Register %u", (unsigned)address);
    return data_logger_log_entry(&e);
}

int data_logger_log_event(const char* description, const char* device_name) {
    log_entry_t e = {0};
    e.timestamp = time(NULL);
    e.type = LOG_TYPE_EVENT;
    e.quality = QUALITY_GOOD;
    if (device_name) strncpy(e.device_name, device_name, sizeof(e.device_name) - 1);
    if (description) strncpy(e.description, description, sizeof(e.description) - 1);
    return data_logger_log_entry(&e);
}

int data_logger_log_alarm(uint16_t address, uint16_t value, const char* description,
                          uint8_t slave_id) {
    log_entry_t e = {0};
    e.timestamp = time(NULL);
    e.type = LOG_TYPE_ALARM;
    e.address = address;
    e.value = value;
    e.slave_id = slave_id;
    e.quality = QUALITY_GOOD;
    if (description) strncpy(e.description, description, sizeof(e.description) - 1);
    return data_logger_log_entry(&e);
}

// ── Stats ────────────────────────────────────────────────────────────────
void data_logger_get_stats(logger_stats_t* stats) {
    if (!stats) return;
    LOCK();
    memcpy(stats, &g_logger.stats, sizeof(logger_stats_t));
    stats->active_tags = g_logger.tag_count;
    UNLOCK();
}

void data_logger_reset_stats(void) {
    LOCK();
    memset(&g_logger.stats, 0, sizeof(g_logger.stats));
    g_logger.stats.logger_start_time = time(NULL);
    UNLOCK();
}

const char* data_logger_get_current_filename(void) { return g_logger.filename; }

uint64_t data_logger_get_current_file_size(void) {
    if (!g_logger.file) return 0;
    struct stat st;
    if (stat(g_logger.filename, &st) == 0) return (uint64_t)st.st_size;
    return 0;
}

// ── Lookup helpers ───────────────────────────────────────────────────────
const char* data_logger_quality_to_string(data_quality_t q) {
    switch (q) {
        case QUALITY_GOOD:        return "GOOD";
        case QUALITY_BAD:         return "BAD";
        case QUALITY_UNCERTAIN:   return "UNCERTAIN";
        case QUALITY_COMM_ERROR:  return "COMM_ERROR";
        default:                  return "UNKNOWN";
    }
}

const char* data_logger_type_to_string(log_entry_type_t t) {
    switch (t) {
        case LOG_TYPE_COIL:             return "COIL";
        case LOG_TYPE_DISCRETE_INPUT:   return "DISCRETE_INPUT";
        case LOG_TYPE_HOLDING_REGISTER: return "HOLDING_REGISTER";
        case LOG_TYPE_INPUT_REGISTER:   return "INPUT_REGISTER";
        case LOG_TYPE_EVENT:            return "EVENT";
        case LOG_TYPE_ALARM:            return "ALARM";
        default:                        return "UNKNOWN";
    }
}

// ── Tag management — stubs (Qt UI schedules its own polling) ─────────────
int data_logger_add_tag(const log_tag_t* tag)              { (void)tag;  return -1; }
int data_logger_remove_tag(int tag_id)                     { (void)tag_id; return -1; }
int data_logger_update_tag(int tag_id, const log_tag_t* t) { (void)tag_id; (void)t; return -1; }
int data_logger_get_tag_count(void)                        { return 0; }
const log_tag_t* data_logger_get_tag(int tag_id)           { (void)tag_id; return NULL; }
void data_logger_clear_tags(void)                          { /* nothing */ }
void data_logger_poll_tags(void* modbus_ctx)               { (void)modbus_ctx; }

// ── Alarm management — stubs (alarm_manager.c is authoritative) ──────────
int data_logger_add_alarm(const alarm_config_t* a)              { (void)a;  return -1; }
int data_logger_remove_alarm(int id)                            { (void)id; return -1; }
int data_logger_update_alarm(int id, const alarm_config_t* a)   { (void)id; (void)a; return -1; }
int data_logger_get_alarm_count(void)                           { return 0; }
const alarm_config_t* data_logger_get_alarm(int id)             { (void)id; return NULL; }
void data_logger_clear_alarms(void)                             { /* nothing */ }
int data_logger_acknowledge_alarm(int id)                       { (void)id; return -1; }
void data_logger_poll_alarms(void* modbus_ctx)                  { (void)modbus_ctx; }

// ── Maintenance — stubs (Phase 5: full impl with FindFirstFile / readdir) ─
int data_logger_cleanup_old_logs(void)                                       { return 0; }
int data_logger_export_to_csv(const char* out, time_t start, time_t end) {
    (void)out; (void)start; (void)end; return -1;
}
int data_logger_compact_logs(void)                                           { return 0; }

// ── Internals ────────────────────────────────────────────────────────────
static int open_new_log_file(void) {
    if (g_logger.file) close_log_file();

    time_t now = time(NULL);
    struct tm* tmi = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tmi);

    snprintf(g_logger.filename, sizeof(g_logger.filename),
             "%s/%s_%s.csv",
             g_logger.config.log_directory,
             g_logger.config.filename_prefix[0] ? g_logger.config.filename_prefix : "modbus_data",
             ts);

    g_logger.file = fopen(g_logger.filename, "a");
    if (!g_logger.file) {
        // Fallback to working directory
        snprintf(g_logger.filename, sizeof(g_logger.filename),
                 "%s_%s.csv",
                 g_logger.config.filename_prefix[0] ? g_logger.config.filename_prefix : "modbus_data",
                 ts);
        g_logger.file = fopen(g_logger.filename, "a");
    }
    if (!g_logger.file) return -1;

    // Write CSV header only if file is empty
    fseek(g_logger.file, 0, SEEK_END);
    if (ftell(g_logger.file) == 0) write_csv_header(g_logger.file);

    g_logger.stats.total_files_created++;
    return 0;
}

static void close_log_file(void) {
    if (!g_logger.file) return;
    fflush(g_logger.file);
    fclose(g_logger.file);
    g_logger.file = NULL;
}

static int write_csv_header(FILE* fp) {
    return fprintf(fp, "Timestamp,Type,SlaveID,Address,Value,Quality,Device,Description\n");
}

static bool should_rotate(void) {
    if (!g_logger.file || g_logger.config.max_file_size_mb <= 0) return false;
    long sz = ftell(g_logger.file);
    return sz > (long)g_logger.config.max_file_size_mb * 1024L * 1024L;
}

static void rotate_if_needed(void) {
    if (should_rotate()) {
        close_log_file();
        open_new_log_file();
    }
}

static void format_timestamp(time_t t, bool include_ms, char* buf, size_t n) {
    struct tm* tmi = localtime(&t);
    if (include_ms) {
        // Note: time_t has 1-second resolution. For true millisecond stamps
        // an extra clock_gettime/QueryPerformanceCounter would be needed —
        // the structure exists for log_entry timestamps populated from
        // higher-resolution sources by the caller (Phase 5 enhancement).
        strftime(buf, n, "%Y-%m-%d %H:%M:%S.000", tmi);
    } else {
        strftime(buf, n, "%Y-%m-%d %H:%M:%S", tmi);
    }
}
