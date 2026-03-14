#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODBUS_TCP = 0,
    MODBUS_RTU
} modbus_type_t;

typedef struct modbus_t modbus_t;

// Create/destroy context
modbus_t* modbus_new_tcp(const char* ip, int port);
modbus_t* modbus_new_rtu(const char* device, int baud, char parity, int data_bits, int stop_bits);
void modbus_free(modbus_t* ctx);

// Connection
int modbus_connect(modbus_t* ctx);
void modbus_close(modbus_t* ctx);

// Configuration
int modbus_set_slave(modbus_t* ctx, int slave);
void modbus_set_response_timeout(modbus_t* ctx, uint32_t sec, uint32_t usec);
void modbus_set_connect_timeout(modbus_t* ctx, int ms);

// Read operations
int modbus_read_registers(modbus_t* ctx, int addr, int nb, uint16_t* dest);
int modbus_read_input_registers(modbus_t* ctx, int addr, int nb, uint16_t* dest);
int modbus_read_bits(modbus_t* ctx, int addr, int nb, uint8_t* dest);
int modbus_read_input_bits(modbus_t* ctx, int addr, int nb, uint8_t* dest);

// Write operations
int modbus_write_register(modbus_t* ctx, int addr, const uint16_t value);
int modbus_write_registers(modbus_t* ctx, int addr, int nb, const uint16_t* src);
int modbus_write_bit(modbus_t* ctx, int addr, int status);
int modbus_write_bits(modbus_t* ctx, int addr, int nb, const uint8_t* src);

// Error handling
const char* modbus_strerror(int errnum);

// Raw serial I/O (RTU gateway use)
int modbus_send_raw(modbus_t* ctx, const uint8_t* buf, int len);
int modbus_recv_raw(modbus_t* ctx, uint8_t* buf, int maxlen, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_H
