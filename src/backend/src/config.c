// Cross-platform INI config loader/saver for ModbusHub.
// Replaces the previous GLib (GKeyFile) implementation so the Qt build
// links cleanly under MinGW without GLib/GTK dependencies.
//
// The format is a subset of INI:
//   [Section]
//   Key=Value
//   ; comment   |  # comment
// Whitespace around '=' is trimmed. Values are read as strings; integer and
// boolean accessors parse on demand. Boolean accepts: 1/0, true/false,
// yes/no, on/off (case-insensitive).
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

app_config_t g_app_config = {0};
static char s_config_path[MAX_PATH_LEN] = {0};

// ── Defaults ──────────────────────────────────────────────────────────────
void config_init(void) {
    memset(&g_app_config, 0, sizeof(app_config_t));

    g_app_config.window_x = -1;
    g_app_config.window_y = -1;
    g_app_config.window_width = 1200;
    g_app_config.window_height = 750;
    g_app_config.window_maximized = false;

    g_app_config.last_mode = 0;
    strncpy(g_app_config.last_ip, "127.0.0.1", sizeof(g_app_config.last_ip) - 1);
    g_app_config.last_port = 502;
#ifdef _WIN32
    strncpy(g_app_config.last_com_port, "COM1", sizeof(g_app_config.last_com_port) - 1);
#else
    strncpy(g_app_config.last_com_port, "/dev/ttyUSB0", sizeof(g_app_config.last_com_port) - 1);
#endif
    g_app_config.last_baud_rate = 9600;
    g_app_config.last_parity = 'N';
    g_app_config.last_slave_id = 1;
    g_app_config.last_poll_interval = 1000;
    g_app_config.last_poll_enabled = false;
    g_app_config.last_display_format = 0;

    g_app_config.gateway_enabled = false;
    g_app_config.gateway_rtu_to_tcp = true;
    strncpy(g_app_config.gateway_tcp_ip, "0.0.0.0", sizeof(g_app_config.gateway_tcp_ip) - 1);
    g_app_config.gateway_tcp_port = 502;
#ifdef _WIN32
    strncpy(g_app_config.gateway_com_port, "COM2", sizeof(g_app_config.gateway_com_port) - 1);
#else
    strncpy(g_app_config.gateway_com_port, "/dev/ttyUSB1", sizeof(g_app_config.gateway_com_port) - 1);
#endif
    g_app_config.gateway_baud_rate = 9600;
    g_app_config.gateway_parity = 'N';

    g_app_config.logger_enabled = true;
    strncpy(g_app_config.log_directory, "logs", sizeof(g_app_config.log_directory) - 1);
    g_app_config.log_max_file_size_mb = 100;
    g_app_config.log_retention_days = 90;
    g_app_config.log_on_change_only = false;
    g_app_config.log_interval_ms = 1000;

    g_app_config.auto_connect_on_startup = false;
    g_app_config.confirm_on_exit = true;
    g_app_config.theme = 0;
#ifdef _WIN32
    g_app_config.enable_event_log = false;
#else
    g_app_config.enable_syslog = true;
#endif
}

const char* config_get_default_path(void) {
    if (s_config_path[0] != '\0') return s_config_path;

#ifdef _WIN32
    char exe[MAX_PATH_LEN];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n > 0 && n < sizeof(exe)) {
        char* slash = strrchr(exe, '\\');
        if (slash) *slash = '\0';
        snprintf(s_config_path, MAX_PATH_LEN, "%s\\%s", exe, CONFIG_FILENAME);
        return s_config_path;
    }
#else
    char exe[MAX_PATH_LEN];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char* slash = strrchr(exe, '/');
        if (slash) *slash = '\0';
        snprintf(s_config_path, MAX_PATH_LEN, "%s/%s", exe, CONFIG_FILENAME);
        return s_config_path;
    }
#endif
    snprintf(s_config_path, MAX_PATH_LEN, "./%s", CONFIG_FILENAME);
    return s_config_path;
}

// ── Parsing helpers ──────────────────────────────────────────────────────
static char* trim(char* s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_bool(const char* v, bool dflt) {
    if (!v) return dflt;
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") ||
        !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return true;
    if (!strcasecmp(v, "0") || !strcasecmp(v, "false") ||
        !strcasecmp(v, "no") || !strcasecmp(v, "off")) return false;
    return dflt;
}

static int parse_int(const char* v, int dflt) {
    if (!v || !*v) return dflt;
    char* end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return dflt;
    return (int)n;
}

// Apply a (section, key, value) tuple to g_app_config. Returns silently on
// unknown keys so newer-version INI files don't break older binaries.
static void apply_kv(const char* section, const char* key, const char* val) {
    if (!section || !key) return;

#define MATCH(s, k) (!strcasecmp(section, (s)) && !strcasecmp(key, (k)))
#define STR_COPY(field) do { \
        if (val) { strncpy(field, val, sizeof(field) - 1); field[sizeof(field) - 1] = '\0'; } \
    } while (0)

    if      (MATCH("Window", "X"))         g_app_config.window_x = parse_int(val, -1);
    else if (MATCH("Window", "Y"))         g_app_config.window_y = parse_int(val, -1);
    else if (MATCH("Window", "Width"))     g_app_config.window_width = parse_int(val, 1200);
    else if (MATCH("Window", "Height"))    g_app_config.window_height = parse_int(val, 750);
    else if (MATCH("Window", "Maximized")) g_app_config.window_maximized = parse_bool(val, false);

    else if (MATCH("Connection", "Mode"))          g_app_config.last_mode = parse_int(val, 0);
    else if (MATCH("Connection", "IP"))            STR_COPY(g_app_config.last_ip);
    else if (MATCH("Connection", "Port"))          g_app_config.last_port = parse_int(val, 502);
    else if (MATCH("Connection", "COMPort"))       STR_COPY(g_app_config.last_com_port);
    else if (MATCH("Connection", "BaudRate"))      g_app_config.last_baud_rate = parse_int(val, 9600);
    else if (MATCH("Connection", "Parity"))        g_app_config.last_parity = (val && val[0]) ? val[0] : 'N';
    else if (MATCH("Connection", "SlaveID"))       g_app_config.last_slave_id = parse_int(val, 1);
    else if (MATCH("Connection", "PollInterval")) g_app_config.last_poll_interval = parse_int(val, 1000);
    else if (MATCH("Connection", "PollEnabled"))  g_app_config.last_poll_enabled = parse_bool(val, false);
    else if (MATCH("Connection", "DisplayFormat")) g_app_config.last_display_format = parse_int(val, 0);

    else if (MATCH("Gateway", "Enabled"))   g_app_config.gateway_enabled = parse_bool(val, false);
    else if (MATCH("Gateway", "RTUtoTCP"))  g_app_config.gateway_rtu_to_tcp = parse_bool(val, true);
    else if (MATCH("Gateway", "TCPIP"))     STR_COPY(g_app_config.gateway_tcp_ip);
    else if (MATCH("Gateway", "TCPPort"))   g_app_config.gateway_tcp_port = parse_int(val, 502);
    else if (MATCH("Gateway", "COMPort"))   STR_COPY(g_app_config.gateway_com_port);
    else if (MATCH("Gateway", "BaudRate"))  g_app_config.gateway_baud_rate = parse_int(val, 9600);
    else if (MATCH("Gateway", "Parity"))    g_app_config.gateway_parity = (val && val[0]) ? val[0] : 'N';

    else if (MATCH("Logger", "Enabled"))         g_app_config.logger_enabled = parse_bool(val, true);
    else if (MATCH("Logger", "Directory"))       STR_COPY(g_app_config.log_directory);
    else if (MATCH("Logger", "MaxFileSizeMB"))   g_app_config.log_max_file_size_mb = parse_int(val, 100);
    else if (MATCH("Logger", "RetentionDays"))   g_app_config.log_retention_days = parse_int(val, 90);
    else if (MATCH("Logger", "LogOnChangeOnly")) g_app_config.log_on_change_only = parse_bool(val, false);
    else if (MATCH("Logger", "IntervalMS"))      g_app_config.log_interval_ms = parse_int(val, 1000);

    else if (MATCH("Application", "AutoConnect"))   g_app_config.auto_connect_on_startup = parse_bool(val, false);
    else if (MATCH("Application", "ConfirmExit"))   g_app_config.confirm_on_exit = parse_bool(val, true);
    else if (MATCH("Application", "Theme"))         g_app_config.theme = parse_int(val, 0);
#ifdef _WIN32
    else if (MATCH("Application", "EnableEventLog")) g_app_config.enable_event_log = parse_bool(val, false);
#else
    else if (MATCH("Application", "EnableSyslog"))   g_app_config.enable_syslog = parse_bool(val, true);
#endif

#undef MATCH
#undef STR_COPY
}

// ── Load / Save ──────────────────────────────────────────────────────────
int config_load(const char* filepath) {
    if (!filepath) return -1;

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        config_init();
        return 0;  // missing file is not an error — we used defaults
    }

    char line[1024];
    char section[64] = "";
    while (fgets(line, sizeof(line), fp)) {
        char* s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char* end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            strncpy(section, s + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
            continue;
        }
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(s);
        char* val = trim(eq + 1);
        apply_kv(section, key, val);
    }

    fclose(fp);
    return 0;
}

int config_save(const char* filepath) {
    if (!filepath) return -1;
    FILE* fp = fopen(filepath, "w");
    if (!fp) return -1;

    fprintf(fp, "[Window]\n");
    fprintf(fp, "X=%d\n", g_app_config.window_x);
    fprintf(fp, "Y=%d\n", g_app_config.window_y);
    fprintf(fp, "Width=%d\n", g_app_config.window_width);
    fprintf(fp, "Height=%d\n", g_app_config.window_height);
    fprintf(fp, "Maximized=%d\n\n", g_app_config.window_maximized ? 1 : 0);

    fprintf(fp, "[Connection]\n");
    fprintf(fp, "Mode=%d\n", g_app_config.last_mode);
    fprintf(fp, "IP=%s\n", g_app_config.last_ip);
    fprintf(fp, "Port=%d\n", g_app_config.last_port);
    fprintf(fp, "COMPort=%s\n", g_app_config.last_com_port);
    fprintf(fp, "BaudRate=%d\n", g_app_config.last_baud_rate);
    fprintf(fp, "Parity=%c\n", g_app_config.last_parity ? g_app_config.last_parity : 'N');
    fprintf(fp, "SlaveID=%d\n", g_app_config.last_slave_id);
    fprintf(fp, "PollInterval=%d\n", g_app_config.last_poll_interval);
    fprintf(fp, "PollEnabled=%d\n", g_app_config.last_poll_enabled ? 1 : 0);
    fprintf(fp, "DisplayFormat=%d\n\n", g_app_config.last_display_format);

    fprintf(fp, "[Gateway]\n");
    fprintf(fp, "Enabled=%d\n", g_app_config.gateway_enabled ? 1 : 0);
    fprintf(fp, "RTUtoTCP=%d\n", g_app_config.gateway_rtu_to_tcp ? 1 : 0);
    fprintf(fp, "TCPIP=%s\n", g_app_config.gateway_tcp_ip);
    fprintf(fp, "TCPPort=%d\n", g_app_config.gateway_tcp_port);
    fprintf(fp, "COMPort=%s\n", g_app_config.gateway_com_port);
    fprintf(fp, "BaudRate=%d\n", g_app_config.gateway_baud_rate);
    fprintf(fp, "Parity=%c\n\n", g_app_config.gateway_parity ? g_app_config.gateway_parity : 'N');

    fprintf(fp, "[Logger]\n");
    fprintf(fp, "Enabled=%d\n", g_app_config.logger_enabled ? 1 : 0);
    fprintf(fp, "Directory=%s\n", g_app_config.log_directory);
    fprintf(fp, "MaxFileSizeMB=%d\n", g_app_config.log_max_file_size_mb);
    fprintf(fp, "RetentionDays=%d\n", g_app_config.log_retention_days);
    fprintf(fp, "LogOnChangeOnly=%d\n", g_app_config.log_on_change_only ? 1 : 0);
    fprintf(fp, "IntervalMS=%d\n\n", g_app_config.log_interval_ms);

    fprintf(fp, "[Application]\n");
    fprintf(fp, "AutoConnect=%d\n", g_app_config.auto_connect_on_startup ? 1 : 0);
    fprintf(fp, "ConfirmExit=%d\n", g_app_config.confirm_on_exit ? 1 : 0);
    fprintf(fp, "Theme=%d\n", g_app_config.theme);
#ifdef _WIN32
    fprintf(fp, "EnableEventLog=%d\n", g_app_config.enable_event_log ? 1 : 0);
#else
    fprintf(fp, "EnableSyslog=%d\n", g_app_config.enable_syslog ? 1 : 0);
#endif

    fclose(fp);
    return 0;
}

int config_load_default(void) { return config_load(config_get_default_path()); }
int config_save_default(void) { return config_save(config_get_default_path()); }

// ── Section helpers ──────────────────────────────────────────────────────
void config_save_window_state(int x, int y, int width, int height, bool maximized) {
    g_app_config.window_x = x;
    g_app_config.window_y = y;
    g_app_config.window_width = width;
    g_app_config.window_height = height;
    g_app_config.window_maximized = maximized;
}

void config_save_connection_settings(int mode, const char* ip, int port,
                                     const char* com_port, int baud, char parity,
                                     int slave_id) {
    g_app_config.last_mode = mode;
    if (ip) { strncpy(g_app_config.last_ip, ip, sizeof(g_app_config.last_ip) - 1);
              g_app_config.last_ip[sizeof(g_app_config.last_ip) - 1] = '\0'; }
    g_app_config.last_port = port;
    if (com_port) { strncpy(g_app_config.last_com_port, com_port, sizeof(g_app_config.last_com_port) - 1);
                    g_app_config.last_com_port[sizeof(g_app_config.last_com_port) - 1] = '\0'; }
    g_app_config.last_baud_rate = baud;
    g_app_config.last_parity = parity;
    g_app_config.last_slave_id = slave_id;
}

void config_save_gateway_settings(bool enabled, bool rtu_to_tcp, const char* tcp_ip,
                                  int tcp_port, const char* com_port, int baud, char parity) {
    g_app_config.gateway_enabled = enabled;
    g_app_config.gateway_rtu_to_tcp = rtu_to_tcp;
    if (tcp_ip) { strncpy(g_app_config.gateway_tcp_ip, tcp_ip, sizeof(g_app_config.gateway_tcp_ip) - 1);
                  g_app_config.gateway_tcp_ip[sizeof(g_app_config.gateway_tcp_ip) - 1] = '\0'; }
    g_app_config.gateway_tcp_port = tcp_port;
    if (com_port) { strncpy(g_app_config.gateway_com_port, com_port, sizeof(g_app_config.gateway_com_port) - 1);
                    g_app_config.gateway_com_port[sizeof(g_app_config.gateway_com_port) - 1] = '\0'; }
    g_app_config.gateway_baud_rate = baud;
    g_app_config.gateway_parity = parity;
}

void config_save_logger_settings(bool enabled, const char* log_dir, int max_size_mb,
                                 int retention_days, bool on_change, int interval_ms) {
    g_app_config.logger_enabled = enabled;
    if (log_dir) { strncpy(g_app_config.log_directory, log_dir, sizeof(g_app_config.log_directory) - 1);
                   g_app_config.log_directory[sizeof(g_app_config.log_directory) - 1] = '\0'; }
    g_app_config.log_max_file_size_mb = max_size_mb;
    g_app_config.log_retention_days = retention_days;
    g_app_config.log_on_change_only = on_change;
    g_app_config.log_interval_ms = interval_ms;
}
