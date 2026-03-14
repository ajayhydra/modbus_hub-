#ifndef PACKET_MONITOR_H
#define PACKET_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PACKET_TX,
    PACKET_RX
} packet_direction_t;

typedef struct {
    uint32_t timestamp;
    packet_direction_t direction;
    uint8_t data[512];
    int length;
    char description[256];
} packet_entry_t;

#define MAX_PACKET_ENTRIES 1000

typedef struct {
    packet_entry_t entries[MAX_PACKET_ENTRIES];
    int count;
    int head;
    bool enabled;
} packet_monitor_t;

// Global packet monitor
extern packet_monitor_t g_packet_monitor;

void packet_monitor_init(void);
void packet_monitor_enable(bool enable);
void packet_monitor_clear(void);
void packet_monitor_log(void* ctx, packet_direction_t direction, const uint8_t* data, int length);
int packet_monitor_get_count(void);
const packet_entry_t* packet_monitor_get_entry(int index);

#endif // PACKET_MONITOR_H
