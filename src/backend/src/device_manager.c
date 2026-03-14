#include "device_manager.h"
#include "modbus.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

// Global device list
static modbus_device_t g_devices[MAX_DEVICES];
static int g_device_count = 0;
static pthread_mutex_t g_device_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

// Initialize device manager
void device_manager_init(void) {
    if (g_initialized) return;
    
    pthread_mutex_init(&g_device_lock, NULL);
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_initialized = true;
}

// Add new device
int device_manager_add(const modbus_device_t* device) {
    if (!device || g_device_count >= MAX_DEVICES) return -1;
    
    pthread_mutex_lock(&g_device_lock);
    
    // Find next available ID
    int new_id = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        bool id_exists = false;
        for (int j = 0; j < g_device_count; j++) {
            if (g_devices[j].id == i) {
                id_exists = true;
                break;
            }
        }
        if (!id_exists) {
            new_id = i;
            break;
        }
    }
    
    // Copy device data
    memcpy(&g_devices[g_device_count], device, sizeof(modbus_device_t));
    g_devices[g_device_count].id = new_id;
    g_devices[g_device_count].ctx = NULL;
    g_devices[g_device_count].connected = false;
    g_devices[g_device_count].consecutive_failures = 0;
    g_devices[g_device_count].successful_reads  = 0;
    g_devices[g_device_count].failed_reads      = 0;
    g_devices[g_device_count].successful_writes = 0;
    g_devices[g_device_count].failed_writes     = 0;
    g_device_count++;
    
    pthread_mutex_unlock(&g_device_lock);
    return new_id;
}

// Remove device
int device_manager_remove(int device_id) {
    pthread_mutex_lock(&g_device_lock);
    
    int index = -1;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].id == device_id) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&g_device_lock);
        return -1;
    }
    
    // Disconnect if connected
    if (g_devices[index].connected) {
        device_manager_disconnect(device_id);
    }
    
    // Shift remaining devices
    for (int i = index; i < g_device_count - 1; i++) {
        memcpy(&g_devices[i], &g_devices[i + 1], sizeof(modbus_device_t));
    }
    g_device_count--;
    
    pthread_mutex_unlock(&g_device_lock);
    return 0;
}

// Update device
int device_manager_update(int device_id, const modbus_device_t* device) {
    if (!device) return -1;
    
    pthread_mutex_lock(&g_device_lock);
    
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].id == device_id) {
            bool was_connected = g_devices[i].connected;
            
            // Disconnect if settings changed
            if (was_connected) {
                device_manager_disconnect(device_id);
            }
            
            // Update device
            memcpy(&g_devices[i], device, sizeof(modbus_device_t));
            g_devices[i].id = device_id;
            g_devices[i].ctx = NULL;
            g_devices[i].connected = false;
            
            pthread_mutex_unlock(&g_device_lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_device_lock);
    return -1;
}

// Get device by ID
modbus_device_t* device_manager_get(int device_id) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].id == device_id) {
            return &g_devices[i];
        }
    }
    return NULL;
}

// Get device count
int device_manager_get_count(void) {
    return g_device_count;
}

// Get device by index
modbus_device_t* device_manager_get_by_index(int index) {
    if (index < 0 || index >= g_device_count) return NULL;
    return &g_devices[index];
}

// Connect device
int device_manager_connect(int device_id) {
    modbus_device_t* device = device_manager_get(device_id);
    if (!device) return -1;
    
    if (device->connected) return 0; // Already connected
    
    // Create context based on type
    if (device->type == DEVICE_TYPE_TCP) {
        device->ctx = modbus_new_tcp(device->ip_address, device->port);
    } else {
        device->ctx = modbus_new_rtu(device->com_port, device->baud_rate, 
                                     device->parity, device->data_bits, device->stop_bits);
    }
    
    if (!device->ctx) {
        snprintf(device->last_error, sizeof(device->last_error), "Failed to create context");
        device->last_error_time = time(NULL);
        return -1;
    }

    /* Use a short connect timeout for background auto-reconnects so the
       UI thread is not blocked for 3 seconds when a device is unreachable. */
    modbus_set_connect_timeout(device->ctx, 800);
    
    // Set slave ID
    modbus_set_slave(device->ctx, device->slave_id);
    
    // Connect
    if (modbus_connect(device->ctx) == -1) {
        snprintf(device->last_error, sizeof(device->last_error), "Connection failed");
        device->last_error_time = time(NULL);
        modbus_free(device->ctx);
        device->ctx = NULL;
        return -1;
    }
    
    device->connected = true;
    device->consecutive_failures = 0;
    device->last_poll = time(NULL);
    return 0;
}

// Disconnect device
int device_manager_disconnect(int device_id) {
    modbus_device_t* device = device_manager_get(device_id);
    if (!device) return -1;
    
    if (!device->connected || !device->ctx) return 0;
    
    modbus_close(device->ctx);
    modbus_free(device->ctx);
    device->ctx = NULL;
    device->connected = false;
    
    return 0;
}

// Connect all enabled devices
int device_manager_connect_all(void) {
    int connected = 0;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].enabled && !g_devices[i].connected) {
            if (device_manager_connect(g_devices[i].id) == 0) {
                connected++;
            }
        }
    }
    return connected;
}

// Disconnect all devices
int device_manager_disconnect_all(void) {
    int disconnected = 0;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].connected) {
            if (device_manager_disconnect(g_devices[i].id) == 0) {
                disconnected++;
            }
        }
    }
    return disconnected;
}

// Check if device is connected
bool device_manager_is_connected(int device_id) {
    modbus_device_t* device = device_manager_get(device_id);
    return device ? device->connected : false;
}

// Poll all connected devices
void device_manager_poll_all(void) {
    time_t now = time(NULL);
    
    for (int i = 0; i < g_device_count; i++) {
        if (!g_devices[i].connected || !g_devices[i].enabled) continue;
        if (g_devices[i].poll_interval_ms == 0) continue;
        
        time_t elapsed = now - g_devices[i].last_poll;
        if (elapsed * 1000 >= g_devices[i].poll_interval_ms) {
            g_devices[i].last_poll = now;
            // Poll logic would go here (read registers, etc.)
        }
    }
}

// Clear all devices
void device_manager_clear_all(void) {
    pthread_mutex_lock(&g_device_lock);
    
    // Disconnect all
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].connected) {
            device_manager_disconnect(g_devices[i].id);
        }
    }
    
    g_device_count = 0;
    memset(g_devices, 0, sizeof(g_devices));
    
    pthread_mutex_unlock(&g_device_lock);
}

// Find device by name
int device_manager_find_by_name(const char* name) {
    if (!name) return -1;
    
    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].name, name) == 0) {
            return g_devices[i].id;
        }
    }
    return -1;
}
