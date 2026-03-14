#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Connection type
typedef enum {
    DEVICE_TYPE_TCP = 0,
    DEVICE_TYPE_RTU = 1
} device_connection_type_t;

// Device configuration and state
typedef struct {
    int id;                          // Unique device ID
    char name[64];                   // Device name
    bool enabled;                    // Active/inactive
    
    // Connection settings
    device_connection_type_t type;   // TCP or RTU
    char ip_address[64];             // For TCP
    int port;                        // For TCP
    char com_port[16];               // For RTU (e.g., "COM1")
    int baud_rate;                   // For RTU
    char parity;                     // For RTU ('N', 'E', 'O')
    int data_bits;                   // For RTU
    int stop_bits;                   // For RTU
    uint8_t slave_id;                // Modbus slave ID
    
    // State
    void* ctx;                       // Modbus context (opaque pointer)
    bool connected;                  // Connection status
    time_t last_poll;                // Last communication time
    int poll_interval_ms;            // Auto-polling interval (0=disabled)
    
    // Polling target register
    uint16_t poll_register;              // Register address to poll (default 0)

    // Statistics
    uint32_t successful_reads;
    uint32_t failed_reads;
    uint32_t successful_writes;
    uint32_t failed_writes;
    int      consecutive_failures;   /* reset to 0 on any success; disconnect after threshold */
    time_t last_error_time;
    char last_error[128];
} modbus_device_t;

#define MAX_DEVICES 32

// Initialize device manager
void device_manager_init(void);

// Device CRUD operations
int device_manager_add(const modbus_device_t* device);
int device_manager_remove(int device_id);
int device_manager_update(int device_id, const modbus_device_t* device);
modbus_device_t* device_manager_get(int device_id);
int device_manager_get_count(void);
modbus_device_t* device_manager_get_by_index(int index);

// Connection management
int device_manager_connect(int device_id);
int device_manager_disconnect(int device_id);
int device_manager_connect_all(void);
int device_manager_disconnect_all(void);
bool device_manager_is_connected(int device_id);

// Polling
void device_manager_poll_all(void);

// Utility
void device_manager_clear_all(void);
int device_manager_find_by_name(const char* name);

// Persistence – save/load all commissioned devices
// Returns 0 on success (save) or number of devices loaded (load), -1 on error
int device_manager_save(const char* filepath);
int device_manager_load(const char* filepath);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_MANAGER_H
