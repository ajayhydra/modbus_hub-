#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Configuration file path
#define CONFIG_FILENAME "modbus_master.ini"
#define MAX_PATH_LEN 260

// Configuration structure
typedef struct {
    // Window settings
    int window_x;
    int window_y;
    int window_width;
    int window_height;
    bool window_maximized;
    
    // Last connection settings
    int last_mode;              // 0=TCP, 1=RTU
    char last_ip[64];
    int last_port;
    char last_com_port[16];
    int last_baud_rate;
    char last_parity;
    int last_slave_id;
    int last_poll_interval;
    bool last_poll_enabled;
    int last_display_format;    // 0=Decimal, 1=Hex, 2=Octal
    
    // Gateway settings
    bool gateway_enabled;
    bool gateway_rtu_to_tcp;    // true=RTU-to-TCP, false=TCP-to-RTU
    char gateway_tcp_ip[64];
    int gateway_tcp_port;
    char gateway_com_port[16];
    int gateway_baud_rate;
    char gateway_parity;
    
    // Logger settings
    bool logger_enabled;
    char log_directory[MAX_PATH_LEN];
    int log_max_file_size_mb;
    int log_retention_days;
    bool log_on_change_only;
    int log_interval_ms;
    
    // Application settings
    bool auto_connect_on_startup;
    bool confirm_on_exit;
    int theme;                  // 0=Light, 1=Dark
#ifdef _WIN32
    bool enable_event_log;      // Windows Event Log integration
#else
    bool enable_syslog;         // syslog integration
#endif
} app_config_t;

// Global configuration instance
extern app_config_t g_app_config;

// Initialize configuration with defaults
void config_init(void);

// Load configuration from INI file
// Returns: 0 on success, -1 on error
int config_load(const char* filepath);

// Save configuration to INI file
// Returns: 0 on success, -1 on error
int config_save(const char* filepath);

// Load configuration from default location
// Returns: 0 on success, -1 on error (uses defaults if file not found)
int config_load_default(void);

// Save configuration to default location
// Returns: 0 on success, -1 on error
int config_save_default(void);

// Get full path to config file
// Returns: pointer to static buffer with full path
const char* config_get_default_path(void);

// Helper functions for specific sections
void config_save_window_state(int x, int y, int width, int height, bool maximized);
void config_save_connection_settings(int mode, const char* ip, int port, 
                                     const char* com_port, int baud, char parity,
                                     int slave_id);
void config_save_gateway_settings(bool enabled, bool rtu_to_tcp, const char* tcp_ip,
                                  int tcp_port, const char* com_port, int baud, char parity);
void config_save_logger_settings(bool enabled, const char* log_dir, int max_size_mb,
                                int retention_days, bool on_change, int interval_ms);

#endif // CONFIG_H
