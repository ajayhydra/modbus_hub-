// Minimal stub implementations for backend functions
// These provide basic functionality for the Qt version
#include "config.h"
#include "app_logger.h"
#include "device_manager.h"
#include "data_logger.h"
#include "packet_monitor.h"
#include "modbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Global config
app_config_t g_app_config;

// Config functions
void config_init(void) {
    // Initialize config with defaults
    memset(&g_app_config, 0, sizeof(g_app_config));
}

int config_load_default(void) {
    strncpy(g_app_config.log_directory, "./logs", sizeof(g_app_config.log_directory) - 1);
    g_app_config.logger_enabled = 1;
    return 0;
}

// App logger functions
int app_logger_init(const app_logger_config_t* config) {
    printf("[AppLogger] Initialized (stub)\n");
    return 0;
}

void app_logger_log(log_level_t level, const char* module, const char* format, ...) {
    // Stub - no actual logging in Qt version
}

// Device manager functions
static modbus_device_t devices[MAX_DEVICES];
static int device_count = 0;
static int next_device_id = 1;

void device_manager_init(void) {
    device_count = 0;
    next_device_id = 1;
    memset(devices, 0, sizeof(devices));
}

int device_manager_add(const modbus_device_t* device) {
    if (device_count >= MAX_DEVICES) return -1;
    
    devices[device_count] = *device;
    devices[device_count].id = next_device_id++;
    devices[device_count].connected = false;
    devices[device_count].ctx = NULL;
    
    int id = devices[device_count].id;
    device_count++;
    return id;
}

int device_manager_remove(int device_id) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].id == device_id) {
            // Disconnect if connected
            if (devices[i].connected && devices[i].ctx) {
                modbus_close((modbus_t*)devices[i].ctx);
                modbus_free((modbus_t*)devices[i].ctx);
            }
            
            // Shift remaining devices
            for (int j = i; j < device_count - 1; j++) {
                devices[j] = devices[j + 1];
            }
            device_count--;
            memset(&devices[device_count], 0, sizeof(modbus_device_t));
            return 0;
        }
    }
    return -1;
}

int device_manager_update(int device_id, const modbus_device_t* device) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].id == device_id) {
            // Preserve connection state and ID
            bool was_connected = devices[i].connected;
            void* ctx = devices[i].ctx;
            int id = devices[i].id;
            
            devices[i] = *device;
            devices[i].id = id;
            devices[i].connected = was_connected;
            devices[i].ctx = ctx;
            return 0;
        }
    }
    return -1;
}

modbus_device_t* device_manager_get(int device_id) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].id == device_id) {
            return &devices[i];
        }
    }
    return NULL;
}

int device_manager_get_count(void) {
    return device_count;
}

modbus_device_t* device_manager_get_by_index(int index) {
    if (index >= 0 && index < device_count) {
        return &devices[index];
    }
    return NULL;
}

int device_manager_connect(int device_id) {
    modbus_device_t* dev = device_manager_get(device_id);
    if (!dev) return -1;
    
    if (dev->connected && dev->ctx) {
        return 0; // Already connected
    }
    
    // Create modbus context
    if (dev->type == DEVICE_TYPE_TCP) {
        dev->ctx = modbus_new_tcp(dev->ip_address, dev->port);
    } else {
        dev->ctx = modbus_new_rtu(dev->com_port, dev->baud_rate, dev->parity, 
                                   dev->data_bits, dev->stop_bits);
    }
    
    if (!dev->ctx) {
        return -1;
    }
    
    modbus_set_slave((modbus_t*)dev->ctx, dev->slave_id);
    
    if (modbus_connect((modbus_t*)dev->ctx) == -1) {
        modbus_free((modbus_t*)dev->ctx);
        dev->ctx = NULL;
        return -1;
    }
    
    dev->connected = true;
    return 0;
}

int device_manager_disconnect(int device_id) {
    modbus_device_t* dev = device_manager_get(device_id);
    if (!dev) return -1;
    
    if (dev->connected && dev->ctx) {
        modbus_close((modbus_t*)dev->ctx);
        modbus_free((modbus_t*)dev->ctx);
        dev->ctx = NULL;
        dev->connected = false;
    }
    
    return 0;
}

int device_manager_connect_all(void) {
    int connected = 0;
    for (int i = 0; i < device_count; i++) {
        if (devices[i].enabled && !devices[i].connected) {
            if (device_manager_connect(devices[i].id) == 0) {
                connected++;
            }
        }
    }
    return connected;
}

int device_manager_disconnect_all(void) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].connected) {
            device_manager_disconnect(devices[i].id);
        }
    }
    return 0;
}

bool device_manager_is_connected(int device_id) {
    modbus_device_t* dev = device_manager_get(device_id);
    return dev ? dev->connected : false;
}

void device_manager_poll_all(void) {
    // Not used in Qt version - polling done via onMonitorAllTimer()
}

void device_manager_clear_all(void) {
    device_manager_disconnect_all();
    device_count = 0;
    next_device_id = 1;
    memset(devices, 0, sizeof(devices));
}

int device_manager_find_by_name(const char* name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            return devices[i].id;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------
// Device persistence – INI-style flat file
// -----------------------------------------------------------------------
#define DEVICE_SAVE_FILENAME "devices.ini"

int device_manager_save(const char* filepath) {
    if (!filepath) filepath = DEVICE_SAVE_FILENAME;

    FILE* fp = fopen(filepath, "w");
    if (!fp) return -1;

    fprintf(fp, "[Devices]\n");
    fprintf(fp, "count=%d\n\n", device_count);

    for (int i = 0; i < device_count; i++) {
        modbus_device_t* d = &devices[i];
        fprintf(fp, "[Device_%d]\n", i);
        fprintf(fp, "id=%d\n",              d->id);
        fprintf(fp, "name=%s\n",            d->name);
        fprintf(fp, "enabled=%d\n",         d->enabled ? 1 : 0);
        fprintf(fp, "type=%d\n",            (int)d->type);
        fprintf(fp, "ip_address=%s\n",      d->ip_address);
        fprintf(fp, "port=%d\n",            d->port);
        fprintf(fp, "com_port=%s\n",        d->com_port);
        fprintf(fp, "baud_rate=%d\n",       d->baud_rate);
        fprintf(fp, "parity=%c\n",          d->parity ? d->parity : 'N');
        fprintf(fp, "data_bits=%d\n",       d->data_bits ? d->data_bits : 8);
        fprintf(fp, "stop_bits=%d\n",       d->stop_bits ? d->stop_bits : 1);
        fprintf(fp, "slave_id=%d\n",        (int)d->slave_id);
        fprintf(fp, "poll_interval_ms=%d\n",(int)d->poll_interval_ms);
        fprintf(fp, "poll_register=%d\n",   (int)d->poll_register);
        fprintf(fp, "successful_reads=%u\n", d->successful_reads);
        fprintf(fp, "failed_reads=%u\n",     d->failed_reads);
        fprintf(fp, "successful_writes=%u\n",d->successful_writes);
        fprintf(fp, "failed_writes=%u\n",    d->failed_writes);
        fprintf(fp, "\n");
    }

    fclose(fp);
    return 0;
}

int device_manager_load(const char* filepath) {
    if (!filepath) filepath = DEVICE_SAVE_FILENAME;

    FILE* fp = fopen(filepath, "r");
    if (!fp) return -1;   /* No backup file – that is normal on first run */

    /* Reset without disconnecting (we are just starting up) */
    device_count   = 0;
    next_device_id = 1;
    memset(devices, 0, sizeof(devices));

    char line[512];
    int  cur = -1;   /* index into devices[], or -2 for [Devices] section */

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing CR/LF */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (line[0] == '[') {
            if (strncmp(line, "[Devices]", 9) == 0) {
                cur = -2;
            } else if (strncmp(line, "[Device_", 8) == 0) {
                cur = atoi(line + 8);
                if (cur >= 0 && cur < MAX_DEVICES) {
                    memset(&devices[cur], 0, sizeof(modbus_device_t));
                    devices[cur].data_bits = 8;
                    devices[cur].stop_bits = 1;
                    devices[cur].parity    = 'N';
                    if (cur >= device_count)
                        device_count = cur + 1;
                } else {
                    cur = -1;  /* out of range – skip */
                }
            }
        } else if (line[0] != '\0' && line[0] != '#' && line[0] != ';') {
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            const char* key = line;
            const char* val = eq + 1;

            if (cur >= 0 && cur < MAX_DEVICES) {
                modbus_device_t* d = &devices[cur];
                if      (!strcmp(key,"id"))               { d->id = atoi(val); if (d->id >= next_device_id) next_device_id = d->id + 1; }
                else if (!strcmp(key,"name"))              { strncpy(d->name,       val, sizeof(d->name)-1); }
                else if (!strcmp(key,"enabled"))           { d->enabled         = atoi(val) != 0; }
                else if (!strcmp(key,"type"))              { d->type            = (device_connection_type_t)atoi(val); }
                else if (!strcmp(key,"ip_address"))        { strncpy(d->ip_address, val, sizeof(d->ip_address)-1); }
                else if (!strcmp(key,"port"))              { d->port            = atoi(val); }
                else if (!strcmp(key,"com_port"))          { strncpy(d->com_port,   val, sizeof(d->com_port)-1); }
                else if (!strcmp(key,"baud_rate"))         { d->baud_rate       = atoi(val); }
                else if (!strcmp(key,"parity"))            { d->parity          = val[0] ? val[0] : 'N'; }
                else if (!strcmp(key,"data_bits"))         { d->data_bits       = atoi(val); }
                else if (!strcmp(key,"stop_bits"))         { d->stop_bits       = atoi(val); }
                else if (!strcmp(key,"slave_id"))          { d->slave_id        = (uint8_t)atoi(val); }
                else if (!strcmp(key,"poll_interval_ms"))  { d->poll_interval_ms= atoi(val); }
                else if (!strcmp(key,"poll_register"))     { d->poll_register   = (uint16_t)atoi(val); }
                else if (!strcmp(key,"successful_reads"))  { d->successful_reads  = (uint32_t)strtoul(val,NULL,10); }
                else if (!strcmp(key,"failed_reads"))      { d->failed_reads      = (uint32_t)strtoul(val,NULL,10); }
                else if (!strcmp(key,"successful_writes")) { d->successful_writes = (uint32_t)strtoul(val,NULL,10); }
                else if (!strcmp(key,"failed_writes"))     { d->failed_writes     = (uint32_t)strtoul(val,NULL,10); }
            }
        }
    }

    fclose(fp);
    return device_count;   /* number of devices successfully restored */
}


int data_logger_init(const logger_config_t* config) {
    (void)config;  // Unused
    return 0;
}

// Packet monitor stubs
void packet_monitor_init(void) {
    // Stub - packet monitoring not implemented in Qt version
}

void packet_monitor_clear(void) {
    // Stub
}
