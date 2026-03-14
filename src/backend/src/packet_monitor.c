#include "packet_monitor.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>

packet_monitor_t g_packet_monitor = {0};

static uint32_t get_tick_count(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

void packet_monitor_init(void) {
    memset(&g_packet_monitor, 0, sizeof(packet_monitor_t));
    g_packet_monitor.enabled = true;
}

void packet_monitor_enable(bool enable) {
    g_packet_monitor.enabled = enable;
}

void packet_monitor_clear(void) {
    g_packet_monitor.count = 0;
    g_packet_monitor.head = 0;
}

static const char* get_function_name(uint8_t function_code) {
    switch (function_code) {
        case 0x01: return "Read Coils";
        case 0x02: return "Read Discrete Inputs";
        case 0x03: return "Read Holding Registers";
        case 0x04: return "Read Input Registers";
        case 0x05: return "Write Single Coil";
        case 0x06: return "Write Single Register";
        case 0x0F: return "Write Multiple Coils";
        case 0x10: return "Write Multiple Registers";
        default: return "Unknown";
    }
}

void packet_monitor_log(void* ctx_ptr, packet_direction_t direction, const uint8_t* data, int length) {
    if (!g_packet_monitor.enabled || data == NULL || length <= 0) {
        return;
    }
    
    int index = g_packet_monitor.head;
    packet_entry_t* entry = &g_packet_monitor.entries[index];
    
    entry->timestamp = get_tick_count();
    entry->direction = direction;
    entry->length = (length > (int)sizeof(entry->data)) ? (int)sizeof(entry->data) : length;
    memcpy(entry->data, data, entry->length);
    
    // Parse packet for description - generic version without modbus_t dependency
    uint8_t func_code = 0;
    if (length >= 8) {
        // Assume TCP format
        uint16_t trans_id = (data[0] << 8) | data[1];
        uint8_t unit_id = data[6];
        func_code = data[7];
        
        sprintf(entry->description, "%s - Trans:%d Unit:%d Func:%02X (%s)",
                direction == PACKET_TX ? "TX" : "RX",
                trans_id, unit_id, func_code, get_function_name(func_code));
    } else if (length >= 2) {
        // Assume RTU format
        uint8_t slave_id = data[0];
        func_code = data[1];
        
        sprintf(entry->description, "%s - Slave:%d Func:%02X (%s)",
                direction == PACKET_TX ? "TX" : "RX",
                slave_id, func_code, get_function_name(func_code));
    } else {
        sprintf(entry->description, "%s - %d bytes",
                direction == PACKET_TX ? "TX" : "RX", length);
    }
    
    g_packet_monitor.head = (g_packet_monitor.head + 1) % MAX_PACKET_ENTRIES;
    if (g_packet_monitor.count < MAX_PACKET_ENTRIES) {
        g_packet_monitor.count++;
    }
}

int packet_monitor_get_count(void) {
    return g_packet_monitor.count;
}

const packet_entry_t* packet_monitor_get_entry(int index) {
    if (index < 0 || index >= g_packet_monitor.count) {
        return NULL;
    }
    
    int actual_index = (g_packet_monitor.head - g_packet_monitor.count + index + MAX_PACKET_ENTRIES) % MAX_PACKET_ENTRIES;
    return &g_packet_monitor.entries[actual_index];
}
