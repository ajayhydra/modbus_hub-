#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>
#include <limits.h>

// Global configuration instance
app_config_t g_app_config = {0};

// Static buffer for config path
static char s_config_path[MAX_PATH_LEN] = {0};

// Initialize configuration with defaults
void config_init(void) {
    memset(&g_app_config, 0, sizeof(app_config_t));
    
    // Window defaults
    g_app_config.window_x = -1;  // Let window manager decide
    g_app_config.window_y = -1;
    g_app_config.window_width = 1200;
    g_app_config.window_height = 750;
    g_app_config.window_maximized = false;
    
    // Connection defaults
    g_app_config.last_mode = 0; // TCP
    strcpy(g_app_config.last_ip, "127.0.0.1");
    g_app_config.last_port = 502;
    strcpy(g_app_config.last_com_port, "/dev/ttyUSB0");
    g_app_config.last_baud_rate = 9600;
    g_app_config.last_parity = 'N';
    g_app_config.last_slave_id = 1;
    g_app_config.last_poll_interval = 1000;
    g_app_config.last_poll_enabled = false;
    g_app_config.last_display_format = 0; // Decimal
    
    // Gateway defaults
    g_app_config.gateway_enabled = false;
    g_app_config.gateway_rtu_to_tcp = true;
    strcpy(g_app_config.gateway_tcp_ip, "0.0.0.0");
    g_app_config.gateway_tcp_port = 502;
    strcpy(g_app_config.gateway_com_port, "/dev/ttyUSB1");
    g_app_config.gateway_baud_rate = 9600;
    g_app_config.gateway_parity = 'N';
    
    // Logger defaults
    g_app_config.logger_enabled = true;
    strcpy(g_app_config.log_directory, "logs");
    g_app_config.log_max_file_size_mb = 100;
    g_app_config.log_retention_days = 90;
    g_app_config.log_on_change_only = false;
    g_app_config.log_interval_ms = 1000;
    
    // Application defaults
    g_app_config.auto_connect_on_startup = false;
    g_app_config.confirm_on_exit = true;
    g_app_config.theme = 0; // Light
    g_app_config.enable_syslog = true;
}

// Get full path to config file
const char* config_get_default_path(void) {
    if (s_config_path[0] == '\0') {
        char exePath[MAX_PATH_LEN];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        
        if (len != -1) {
            exePath[len] = '\0';
            
            // Get directory of executable
            char* lastSlash = strrchr(exePath, '/');
            if (lastSlash) {
                *lastSlash = '\0';
            }
            
            // Construct config path
            snprintf(s_config_path, MAX_PATH_LEN, "%s/%s", exePath, CONFIG_FILENAME);
        } else {
            // Fallback to current directory
            snprintf(s_config_path, MAX_PATH_LEN, "./%s", CONFIG_FILENAME);
        }
    }
    
    return s_config_path;
}

// Load configuration from INI file
int config_load(const char* filepath) {
    if (!filepath) {
        return -1;
    }
    
    // Check if file exists
    if (access(filepath, F_OK) != 0) {
        // File doesn't exist, use defaults
        config_init();
        return 0;
    }
    
    GKeyFile* keyfile = g_key_file_new();
    GError* error = NULL;
    
    if (!g_key_file_load_from_file(keyfile, filepath, G_KEY_FILE_NONE, &error)) {
        if (error) {
            g_error_free(error);
        }
        g_key_file_free(keyfile);
        config_init();
        return 0;
    }
    
    // Load window settings
    g_app_config.window_x = g_key_file_get_integer(keyfile, "Window", "X", NULL);
    g_app_config.window_y = g_key_file_get_integer(keyfile, "Window", "Y", NULL);
    g_app_config.window_width = g_key_file_get_integer(keyfile, "Window", "Width", NULL);
    if (g_app_config.window_width == 0) g_app_config.window_width = 1200;
    g_app_config.window_height = g_key_file_get_integer(keyfile, "Window", "Height", NULL);
    if (g_app_config.window_height == 0) g_app_config.window_height = 750;
    g_app_config.window_maximized = g_key_file_get_boolean(keyfile, "Window", "Maximized", NULL);
    
    // Load connection settings
    g_app_config.last_mode = g_key_file_get_integer(keyfile, "Connection", "Mode", NULL);
    gchar* str = g_key_file_get_string(keyfile, "Connection", "IP", NULL);
    if (str) {
        strncpy(g_app_config.last_ip, str, sizeof(g_app_config.last_ip) - 1);
        g_free(str);
    }
    g_app_config.last_port = g_key_file_get_integer(keyfile, "Connection", "Port", NULL);
    if (g_app_config.last_port == 0) g_app_config.last_port = 502;
    
    str = g_key_file_get_string(keyfile, "Connection", "COMPort", NULL);
    if (str) {
        strncpy(g_app_config.last_com_port, str, sizeof(g_app_config.last_com_port) - 1);
        g_free(str);
    }
    g_app_config.last_baud_rate = g_key_file_get_integer(keyfile, "Connection", "BaudRate", NULL);
    if (g_app_config.last_baud_rate == 0) g_app_config.last_baud_rate = 9600;
    
    str = g_key_file_get_string(keyfile, "Connection", "Parity", NULL);
    if (str && str[0]) {
        g_app_config.last_parity = str[0];
        g_free(str);
    }
    
    g_app_config.last_slave_id = g_key_file_get_integer(keyfile, "Connection", "SlaveID", NULL);
    if (g_app_config.last_slave_id == 0) g_app_config.last_slave_id = 1;
    g_app_config.last_poll_interval = g_key_file_get_integer(keyfile, "Connection", "PollInterval", NULL);
    if (g_app_config.last_poll_interval == 0) g_app_config.last_poll_interval = 1000;
    g_app_config.last_poll_enabled = g_key_file_get_boolean(keyfile, "Connection", "PollEnabled", NULL);
    g_app_config.last_display_format = g_key_file_get_integer(keyfile, "Connection", "DisplayFormat", NULL);
    
    // Load gateway settings
    g_app_config.gateway_enabled = g_key_file_get_boolean(keyfile, "Gateway", "Enabled", NULL);
    g_app_config.gateway_rtu_to_tcp = g_key_file_get_boolean(keyfile, "Gateway", "RTUtoTCP", NULL);
    str = g_key_file_get_string(keyfile, "Gateway", "TCPIP", NULL);
    if (str) {
        strncpy(g_app_config.gateway_tcp_ip, str, sizeof(g_app_config.gateway_tcp_ip) - 1);
        g_free(str);
    }
    g_app_config.gateway_tcp_port = g_key_file_get_integer(keyfile, "Gateway", "TCPPort", NULL);
    if (g_app_config.gateway_tcp_port == 0) g_app_config.gateway_tcp_port = 502;
    
    str = g_key_file_get_string(keyfile, "Gateway", "COMPort", NULL);
    if (str) {
        strncpy(g_app_config.gateway_com_port, str, sizeof(g_app_config.gateway_com_port) - 1);
        g_free(str);
    }
    g_app_config.gateway_baud_rate = g_key_file_get_integer(keyfile, "Gateway", "BaudRate", NULL);
    if (g_app_config.gateway_baud_rate == 0) g_app_config.gateway_baud_rate = 9600;
    
    str = g_key_file_get_string(keyfile, "Gateway", "Parity", NULL);
    if (str && str[0]) {
        g_app_config.gateway_parity = str[0];
        g_free(str);
    }
    
    // Load logger settings
    g_app_config.logger_enabled = g_key_file_get_boolean(keyfile, "Logger", "Enabled", NULL);
    str = g_key_file_get_string(keyfile, "Logger", "Directory", NULL);
    if (str) {
        strncpy(g_app_config.log_directory, str, sizeof(g_app_config.log_directory) - 1);
        g_free(str);
    }
    g_app_config.log_max_file_size_mb = g_key_file_get_integer(keyfile, "Logger", "MaxFileSizeMB", NULL);
    if (g_app_config.log_max_file_size_mb == 0) g_app_config.log_max_file_size_mb = 100;
    g_app_config.log_retention_days = g_key_file_get_integer(keyfile, "Logger", "RetentionDays", NULL);
    if (g_app_config.log_retention_days == 0) g_app_config.log_retention_days = 90;
    g_app_config.log_on_change_only = g_key_file_get_boolean(keyfile, "Logger", "LogOnChangeOnly", NULL);
    g_app_config.log_interval_ms = g_key_file_get_integer(keyfile, "Logger", "IntervalMS", NULL);
    if (g_app_config.log_interval_ms == 0) g_app_config.log_interval_ms = 1000;
    
    // Load application settings
    g_app_config.auto_connect_on_startup = g_key_file_get_boolean(keyfile, "Application", "AutoConnect", NULL);
    g_app_config.confirm_on_exit = g_key_file_get_boolean(keyfile, "Application", "ConfirmExit", NULL);
    g_app_config.theme = g_key_file_get_integer(keyfile, "Application", "Theme", NULL);
    g_app_config.enable_syslog = g_key_file_get_boolean(keyfile, "Application", "EnableSyslog", NULL);
    
    g_key_file_free(keyfile);
    return 0;
}

// Save configuration to INI file
int config_save(const char* filepath) {
    if (!filepath) {
        return -1;
    }
    
    GKeyFile* keyfile = g_key_file_new();
    
    // Save window settings
    g_key_file_set_integer(keyfile, "Window", "X", g_app_config.window_x);
    g_key_file_set_integer(keyfile, "Window", "Y", g_app_config.window_y);
    g_key_file_set_integer(keyfile, "Window", "Width", g_app_config.window_width);
    g_key_file_set_integer(keyfile, "Window", "Height", g_app_config.window_height);
    g_key_file_set_boolean(keyfile, "Window", "Maximized", g_app_config.window_maximized);
    
    // Save connection settings
    g_key_file_set_integer(keyfile, "Connection", "Mode", g_app_config.last_mode);
    g_key_file_set_string(keyfile, "Connection", "IP", g_app_config.last_ip);
    g_key_file_set_integer(keyfile, "Connection", "Port", g_app_config.last_port);
    g_key_file_set_string(keyfile, "Connection", "COMPort", g_app_config.last_com_port);
    g_key_file_set_integer(keyfile, "Connection", "BaudRate", g_app_config.last_baud_rate);
    
    char parity_str[2] = {g_app_config.last_parity, '\0'};
    g_key_file_set_string(keyfile, "Connection", "Parity", parity_str);
    
    g_key_file_set_integer(keyfile, "Connection", "SlaveID", g_app_config.last_slave_id);
    g_key_file_set_integer(keyfile, "Connection", "PollInterval", g_app_config.last_poll_interval);
    g_key_file_set_boolean(keyfile, "Connection", "PollEnabled", g_app_config.last_poll_enabled);
    g_key_file_set_integer(keyfile, "Connection", "DisplayFormat", g_app_config.last_display_format);
    
    // Save gateway settings
    g_key_file_set_boolean(keyfile, "Gateway", "Enabled", g_app_config.gateway_enabled);
    g_key_file_set_boolean(keyfile, "Gateway", "RTUtoTCP", g_app_config.gateway_rtu_to_tcp);
    g_key_file_set_string(keyfile, "Gateway", "TCPIP", g_app_config.gateway_tcp_ip);
    g_key_file_set_integer(keyfile, "Gateway", "TCPPort", g_app_config.gateway_tcp_port);
    g_key_file_set_string(keyfile, "Gateway", "COMPort", g_app_config.gateway_com_port);
    g_key_file_set_integer(keyfile, "Gateway", "BaudRate", g_app_config.gateway_baud_rate);
    
    parity_str[0] = g_app_config.gateway_parity;
    g_key_file_set_string(keyfile, "Gateway", "Parity", parity_str);
    
    // Save logger settings
    g_key_file_set_boolean(keyfile, "Logger", "Enabled", g_app_config.logger_enabled);
    g_key_file_set_string(keyfile, "Logger", "Directory", g_app_config.log_directory);
    g_key_file_set_integer(keyfile, "Logger", "MaxFileSizeMB", g_app_config.log_max_file_size_mb);
    g_key_file_set_integer(keyfile, "Logger", "RetentionDays", g_app_config.log_retention_days);
    g_key_file_set_boolean(keyfile, "Logger", "LogOnChangeOnly", g_app_config.log_on_change_only);
    g_key_file_set_integer(keyfile, "Logger", "IntervalMS", g_app_config.log_interval_ms);
    
    // Save application settings
    g_key_file_set_boolean(keyfile, "Application", "AutoConnect", g_app_config.auto_connect_on_startup);
    g_key_file_set_boolean(keyfile, "Application", "ConfirmExit", g_app_config.confirm_on_exit);
    g_key_file_set_integer(keyfile, "Application", "Theme", g_app_config.theme);
    g_key_file_set_boolean(keyfile, "Application", "EnableSyslog", g_app_config.enable_syslog);
    
    // Write to file
    GError* error = NULL;
    gsize length;
    gchar* data = g_key_file_to_data(keyfile, &length, &error);
    
    int result = -1;
    if (data) {
        FILE* file = fopen(filepath, "w");
        if (file) {
            fwrite(data, 1, length, file);
            fclose(file);
            result = 0;
        }
        g_free(data);
    }
    
    if (error) {
        g_error_free(error);
    }
    
    g_key_file_free(keyfile);
    return result;
}

// Load configuration from default location
int config_load_default(void) {
    return config_load(config_get_default_path());
}

// Save configuration to default location
int config_save_default(void) {
    return config_save(config_get_default_path());
}

// Helper: Save window state
void config_save_window_state(int x, int y, int width, int height, bool maximized) {
    g_app_config.window_x = x;
    g_app_config.window_y = y;
    g_app_config.window_width = width;
    g_app_config.window_height = height;
    g_app_config.window_maximized = maximized;
}

// Helper: Save connection settings
void config_save_connection_settings(int mode, const char* ip, int port,
                                    const char* com_port, int baud, char parity,
                                    int slave_id) {
    g_app_config.last_mode = mode;
    if (ip) strncpy(g_app_config.last_ip, ip, sizeof(g_app_config.last_ip) - 1);
    g_app_config.last_port = port;
    if (com_port) strncpy(g_app_config.last_com_port, com_port, sizeof(g_app_config.last_com_port) - 1);
    g_app_config.last_baud_rate = baud;
    g_app_config.last_parity = parity;
    g_app_config.last_slave_id = slave_id;
}

// Helper: Save gateway settings
void config_save_gateway_settings(bool enabled, bool rtu_to_tcp, const char* tcp_ip,
                                 int tcp_port, const char* com_port, int baud, char parity) {
    g_app_config.gateway_enabled = enabled;
    g_app_config.gateway_rtu_to_tcp = rtu_to_tcp;
    if (tcp_ip) strncpy(g_app_config.gateway_tcp_ip, tcp_ip, sizeof(g_app_config.gateway_tcp_ip) - 1);
    g_app_config.gateway_tcp_port = tcp_port;
    if (com_port) strncpy(g_app_config.gateway_com_port, com_port, sizeof(g_app_config.gateway_com_port) - 1);
    g_app_config.gateway_baud_rate = baud;
    g_app_config.gateway_parity = parity;
}

// Helper: Save logger settings
void config_save_logger_settings(bool enabled, const char* log_dir, int max_size_mb,
                                int retention_days, bool on_change, int interval_ms) {
    g_app_config.logger_enabled = enabled;
    if (log_dir) strncpy(g_app_config.log_directory, log_dir, sizeof(g_app_config.log_directory) - 1);
    g_app_config.log_max_file_size_mb = max_size_mb;
    g_app_config.log_retention_days = retention_days;
    g_app_config.log_on_change_only = on_change;
    g_app_config.log_interval_ms = interval_ms;
}
