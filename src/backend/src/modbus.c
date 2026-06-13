#include "modbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define SOCKET_TYPE SOCKET
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    /* TCP */
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET  -1
    #define SOCKET_ERROR    -1
    #define closesocket close
    typedef int SOCKET_TYPE;
    #define INVALID_SOCKET_VALUE -1
    /* RTU (serial) */
    #include <termios.h>
    #include <fcntl.h>
    #include <sys/select.h>
#endif

/* Modbus function codes */
#define MODBUS_FC_READ_COILS                0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS      0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04
#define MODBUS_FC_WRITE_SINGLE_COIL         0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS      0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

struct modbus_t {
    modbus_type_t type;
    /* TCP */
    SOCKET_TYPE  socket;
    char         ip[64];
    int          port;
    uint16_t     transaction_id;
    /* RTU */
    char         device[64];
    int          baud;
    char         parity;   /* 'N', 'E', 'O' */
    int          data_bits;
    int          stop_bits;
#ifdef _WIN32
    HANDLE       serial_handle;
#else
    int          serial_fd;
#endif
    /* Common */
    int          slave_id;
    int          timeout_ms;         /* response timeout */
    int          connect_timeout_ms; /* TCP connect timeout (default 3000 ms) */
    int          last_exception_code;/* Last Modbus exception (1..11), 0 if none */
};

modbus_t* modbus_new_tcp(const char* ip, int port) {
    modbus_t* ctx = (modbus_t*)calloc(1, sizeof(modbus_t));
    if (!ctx) return NULL;
    ctx->type           = MODBUS_TCP;
    ctx->socket         = INVALID_SOCKET_VALUE;
    ctx->slave_id           = 1;
    ctx->timeout_ms         = 1000;
    ctx->connect_timeout_ms = 3000;
    ctx->transaction_id     = 0;
    strncpy(ctx->ip, ip, sizeof(ctx->ip) - 1);
    ctx->port = port;
#ifdef _WIN32
    ctx->serial_handle = INVALID_HANDLE_VALUE;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    ctx->serial_fd = -1;
#endif
    return ctx;
}

modbus_t* modbus_new_rtu(const char* device, int baud, char parity,
                         int data_bits, int stop_bits) {
    modbus_t* ctx = (modbus_t*)calloc(1, sizeof(modbus_t));
    if (!ctx) return NULL;
    ctx->type      = MODBUS_RTU;
    ctx->socket    = INVALID_SOCKET_VALUE;
    ctx->slave_id           = 1;
    ctx->timeout_ms         = 1000;
    ctx->connect_timeout_ms = 3000;
    strncpy(ctx->device, device, sizeof(ctx->device) - 1);
    ctx->device[sizeof(ctx->device) - 1] = '\0';
    ctx->baud      = baud;
    ctx->parity    = parity;
    ctx->data_bits = data_bits;
    ctx->stop_bits = stop_bits;
#ifdef _WIN32
    ctx->serial_handle = INVALID_HANDLE_VALUE;
#else
    ctx->serial_fd = -1;
#endif
    return ctx;
}

void modbus_free(modbus_t* ctx) {
    if (!ctx) return;
    modbus_close(ctx);
#ifdef _WIN32
    if (ctx->type == MODBUS_TCP) WSACleanup();
#endif
    free(ctx);
}

int modbus_connect(modbus_t* ctx) {
    if (!ctx) return -1;

    /* ── TCP ─────────────────────────────────────────────────── */
    if (ctx->type == MODBUS_TCP) {
        struct sockaddr_in server;
        ctx->socket = socket(AF_INET, SOCK_STREAM, 0);
        if (ctx->socket == INVALID_SOCKET_VALUE) return -1;

        server.sin_family      = AF_INET;
        server.sin_port        = htons(ctx->port);
        server.sin_addr.s_addr = inet_addr(ctx->ip);

        /* Set socket to non-blocking so connect() returns immediately */
#ifdef _WIN32
        u_long nb_mode = 1;
        ioctlsocket(ctx->socket, FIONBIO, &nb_mode);
#else
        {
            int flags = fcntl(ctx->socket, F_GETFL, 0);
            fcntl(ctx->socket, F_SETFL, flags | O_NONBLOCK);
        }
#endif
        int rc = connect(ctx->socket, (struct sockaddr*)&server, sizeof(server));
#ifdef _WIN32
        int in_progress = (rc < 0 && WSAGetLastError() == WSAEWOULDBLOCK);
#else
        int in_progress = (rc < 0 && errno == EINPROGRESS);
#endif
        if (rc < 0 && !in_progress) {
            closesocket(ctx->socket);
            ctx->socket = INVALID_SOCKET_VALUE;
            return -1;
        }
        if (in_progress) {
            /* Wait for the socket to become writable (= connect done) */
            fd_set wfds, efds;
            struct timeval tv;
            FD_ZERO(&wfds); FD_SET(ctx->socket, &wfds);
            FD_ZERO(&efds); FD_SET(ctx->socket, &efds);
            tv.tv_sec  = ctx->connect_timeout_ms / 1000;
            tv.tv_usec = (ctx->connect_timeout_ms % 1000) * 1000;
            int sel = select((int)ctx->socket + 1, NULL, &wfds, &efds, &tv);
            if (sel <= 0 || FD_ISSET(ctx->socket, &efds)) {
                /* Timeout or error */
                closesocket(ctx->socket);
                ctx->socket = INVALID_SOCKET_VALUE;
                return -1;
            }
            /* Confirm the connection really succeeded */
            int err = 0;
            socklen_t errlen = sizeof(err);
            getsockopt(ctx->socket, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
            if (err != 0) {
                closesocket(ctx->socket);
                ctx->socket = INVALID_SOCKET_VALUE;
                return -1;
            }
        }

        /* Restore blocking mode */
#ifdef _WIN32
        u_long blk_mode = 0;
        ioctlsocket(ctx->socket, FIONBIO, &blk_mode);
#else
        {
            int flags = fcntl(ctx->socket, F_GETFL, 0);
            fcntl(ctx->socket, F_SETFL, flags & ~O_NONBLOCK);
        }
#endif
        /* Set response timeouts now that the socket is connected */
        struct timeval rtv;
        rtv.tv_sec  = ctx->timeout_ms / 1000;
        rtv.tv_usec = (ctx->timeout_ms % 1000) * 1000;
        setsockopt(ctx->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rtv, sizeof(rtv));
        setsockopt(ctx->socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&rtv, sizeof(rtv));
        return 0;
    }

    /* ── RTU (serial) ───────────────────────────────────────── */
#ifdef _WIN32
    /* Windows requires the "\\.\" prefix for reliable COM port access.
       Without it CreateFile returns ERROR_PATH_NOT_FOUND for any COMx. */
    char win_device[72];
    if (strncmp(ctx->device, "\\\\.\\", 4) == 0) {
        strncpy(win_device, ctx->device, sizeof(win_device) - 1);
    } else {
        snprintf(win_device, sizeof(win_device), "\\\\.\\%s", ctx->device);
    }
    win_device[sizeof(win_device) - 1] = '\0';
    ctx->serial_handle = CreateFileA(win_device,
                                     GENERIC_READ | GENERIC_WRITE,
                                     0, NULL, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (ctx->serial_handle == INVALID_HANDLE_VALUE) {
        /* Map Windows error to errno so callers can print it */
        DWORD we = GetLastError();
        errno = (we == ERROR_ACCESS_DENIED)  ? EACCES :
                (we == ERROR_FILE_NOT_FOUND ||
                 we == ERROR_PATH_NOT_FOUND) ? ENOENT : EIO;
        return -1;
    }
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(ctx->serial_handle, &dcb)) {
        CloseHandle(ctx->serial_handle);
        ctx->serial_handle = INVALID_HANDLE_VALUE;
        errno = EIO; return -1;
    }
    dcb.BaudRate     = (DWORD)ctx->baud;
    dcb.ByteSize     = (BYTE)ctx->data_bits;
    dcb.Parity       = (ctx->parity == 'E') ? EVENPARITY :
                       (ctx->parity == 'O') ? ODDPARITY  : NOPARITY;
    dcb.StopBits     = (ctx->stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary      = TRUE;
    dcb.fParity      = (dcb.Parity != NOPARITY) ? TRUE : FALSE;
    /* Disable all hardware/software flow control */
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX        = FALSE;
    dcb.fInX         = FALSE;
    dcb.fDsrSensitivity = FALSE;
    if (!SetCommState(ctx->serial_handle, &dcb)) {
        CloseHandle(ctx->serial_handle);
        ctx->serial_handle = INVALID_HANDLE_VALUE;
        errno = EIO; return -1;
    }
    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout         = 10;
    timeouts.ReadTotalTimeoutConstant    = ctx->timeout_ms;
    timeouts.ReadTotalTimeoutMultiplier  = 1;
    timeouts.WriteTotalTimeoutConstant   = ctx->timeout_ms;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    SetCommTimeouts(ctx->serial_handle, &timeouts);
    return 0;
#else
    ctx->serial_fd = open(ctx->device, O_RDWR | O_NOCTTY | O_SYNC);
    if (ctx->serial_fd < 0) return -1;
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(ctx->serial_fd, &tty) != 0) {
        close(ctx->serial_fd); ctx->serial_fd = -1; return -1;
    }
    /* Baud rate */
    speed_t spd;
    switch (ctx->baud) {
        case 1200:   spd = B1200;   break;
        case 2400:   spd = B2400;   break;
        case 4800:   spd = B4800;   break;
        case 9600:   spd = B9600;   break;
        case 19200:  spd = B19200;  break;
        case 38400:  spd = B38400;  break;
        case 57600:  spd = B57600;  break;
        case 115200: spd = B115200; break;
        default:     spd = B9600;   break;
    }
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);
    /* Data bits */
    tty.c_cflag &= ~CSIZE;
    switch (ctx->data_bits) {
        case 5:  tty.c_cflag |= CS5; break;
        case 6:  tty.c_cflag |= CS6; break;
        case 7:  tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }
    /* Parity */
    if (ctx->parity == 'E') { tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; }
    else if (ctx->parity == 'O') { tty.c_cflag |= PARENB; tty.c_cflag |= PARODD; }
    else { tty.c_cflag &= ~PARENB; }
    /* Stop bits */
    if (ctx->stop_bits == 2) tty.c_cflag |= CSTOPB;
    else                     tty.c_cflag &= ~CSTOPB;
    tty.c_cflag |=  (CREAD | CLOCAL);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INPCK | ISTRIP);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = (cc_t)(ctx->timeout_ms / 100);
    if (tty.c_cc[VTIME] == 0) tty.c_cc[VTIME] = 1;
    if (tcsetattr(ctx->serial_fd, TCSANOW, &tty) != 0) {
        close(ctx->serial_fd); ctx->serial_fd = -1; return -1;
    }
    return 0;
#endif
}

void modbus_close(modbus_t* ctx) {
    if (!ctx) return;
    if (ctx->type == MODBUS_TCP) {
        if (ctx->socket != INVALID_SOCKET_VALUE) {
            closesocket(ctx->socket);
            ctx->socket = INVALID_SOCKET_VALUE;
        }
    } else {
#ifdef _WIN32
        if (ctx->serial_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(ctx->serial_handle);
            ctx->serial_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (ctx->serial_fd >= 0) {
            close(ctx->serial_fd);
            ctx->serial_fd = -1;
        }
#endif
    }
}

int modbus_set_slave(modbus_t* ctx, int slave) {
    if (!ctx) return -1;
    ctx->slave_id = slave;
    return 0;
}

void modbus_set_response_timeout(modbus_t* ctx, uint32_t sec, uint32_t usec) {
    if (!ctx) return;
    ctx->timeout_ms = sec * 1000 + usec / 1000;
}

void modbus_set_connect_timeout(modbus_t* ctx, int ms) {
    if (!ctx) return;
    ctx->connect_timeout_ms = ms;
}

// Build Modbus TCP request
static int build_tcp_request(modbus_t* ctx, uint8_t function, uint16_t addr, uint16_t nb, uint8_t* req) {
    int len = 0;
    
    // MBAP Header
    req[len++] = (ctx->transaction_id >> 8) & 0xFF;
    req[len++] = ctx->transaction_id & 0xFF;
    ctx->transaction_id++;
    
    req[len++] = 0x00; // Protocol ID
    req[len++] = 0x00;
    
    // Length will be filled later
    int length_pos = len;
    req[len++] = 0x00;
    req[len++] = 0x00;
    
    req[len++] = ctx->slave_id;
    req[len++] = function;
    req[len++] = (addr >> 8) & 0xFF;
    req[len++] = addr & 0xFF;
    req[len++] = (nb >> 8) & 0xFF;
    req[len++] = nb & 0xFF;
    
    // Fill length field (bytes after length field)
    uint16_t length = len - 6;
    req[length_pos] = (length >> 8) & 0xFF;
    req[length_pos + 1] = length & 0xFF;
    return len;
}

/* ══ RTU helpers ══════════════════════════════════════════════════════════ */

/* CRC-16/IBM (Modbus standard) */
static uint16_t crc16(const uint8_t* buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

static int rtu_check_crc(const uint8_t* buf, int len) {
    if (len < 2) return -1;
    uint16_t calc = crc16(buf, len - 2);
    uint16_t recv = (uint16_t)buf[len-2] | ((uint16_t)buf[len-1] << 8);
    return (calc == recv) ? 0 : -1;
}

/* Build 8-byte RTU request (addr + func + 2-byte addr + 2-byte qty + 2-byte CRC) */
static int build_rtu_request(modbus_t* ctx, uint8_t function,
                              uint16_t addr, uint16_t nb, uint8_t* req) {
    int len = 0;
    req[len++] = (uint8_t)ctx->slave_id;
    req[len++] = function;
    req[len++] = (addr >> 8) & 0xFF;
    req[len++] = addr & 0xFF;
    req[len++] = (nb   >> 8) & 0xFF;
    req[len++] = nb & 0xFF;
    uint16_t crc = crc16(req, len);
    req[len++] = crc & 0xFF;          /* CRC low  */
    req[len++] = (crc >> 8) & 0xFF;   /* CRC high */
    return len;
}

/* Send RTU request over serial and read back up to max_len bytes.
 * Stops when max_len bytes are received OR an inter-character gap timeout
 * fires. Returns total bytes read. Caller is responsible for validating
 * the frame (CRC, length, exception status). */
static int rtu_request(modbus_t* ctx, uint8_t* req, int req_len,
                       uint8_t* rsp, int max_len) {
#ifdef _WIN32
    DWORD written = 0;
    PurgeComm(ctx->serial_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    if (!WriteFile(ctx->serial_handle, req, req_len, &written, NULL) ||
        (int)written != req_len) return -1;
    DWORD total = 0, rd = 0;
    while ((int)total < max_len) {
        if (!ReadFile(ctx->serial_handle, rsp + total,
                      max_len - total, &rd, NULL)) break;
        if (rd == 0) break;
        total += rd;
    }
    return (int)total;
#else
    tcflush(ctx->serial_fd, TCIOFLUSH);
    if (write(ctx->serial_fd, req, req_len) != req_len) return -1;
    int total = 0;
    while (total < max_len) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ctx->serial_fd, &fds);
        tv.tv_sec  = ctx->timeout_ms / 1000;
        tv.tv_usec = (ctx->timeout_ms % 1000) * 1000;
        if (select(ctx->serial_fd + 1, &fds, NULL, NULL, &tv) <= 0) break;
        int rd = read(ctx->serial_fd, rsp + total, max_len - total);
        if (rd <= 0) break;
        total += rd;
    }
    return total;
#endif
}

/* ── TCP framing & exception helpers ─────────────────────────────────── */

/* Read exactly n bytes from the socket, looping over recv() until we have
 * the full count or the socket times out / errors out.
 * The socket already has SO_RCVTIMEO set in modbus_connect(), so a stalled
 * peer eventually returns 0 / -1 here. Returns n on success, -1 on failure. */
static int tcp_recv_n(modbus_t* ctx, uint8_t* buf, int n) {
    int total = 0;
    while (total < n) {
        int rd = recv(ctx->socket, (char*)buf + total, n - total, 0);
        if (rd <= 0) return -1;
        total += rd;
    }
    return total;
}

/* Receive a complete Modbus TCP response: 6-byte MBAP header followed by
 * `length` more bytes (length field counts unit ID + function + payload).
 * Returns total bytes written into rsp (>= 8), or -1 on framing/timeout. */
static int tcp_recv_response(modbus_t* ctx, uint8_t* rsp, int rsp_size) {
    if (rsp_size < 8) return -1;
    if (tcp_recv_n(ctx, rsp, 6) != 6) return -1;
    int length = (rsp[4] << 8) | rsp[5];
    /* length must cover at least unit_id + function_code (2 bytes), and the
     * largest legal Modbus PDU is 253 bytes, so length <= 254. */
    if (length < 2 || length > 254 || length > rsp_size - 6) return -1;
    if (tcp_recv_n(ctx, rsp + 6, length) != length) return -1;
    return 6 + length;
}

/* If the response carries a Modbus exception (function code with high bit
 * set), record the exception code on the context and return 1.
 * Returns 0 for a normal response, -1 for a malformed one. */
static int check_exception_pdu(modbus_t* ctx, const uint8_t* pdu, int pdu_len,
                               uint8_t expected_fc) {
    if (pdu_len < 1) return -1;
    uint8_t fc = pdu[0];
    if (fc == expected_fc) return 0;
    if (fc == (uint8_t)(expected_fc | 0x80)) {
        if (pdu_len < 2) return -1;
        ctx->last_exception_code = pdu[1];
        return 1;
    }
    return -1;
}

/* Validate and classify an RTU response. The PDU is rsp[1..rsp_len-3] (slave
 * address at rsp[0], CRC in last 2 bytes). Sets ctx->last_exception_code on
 * exception. Returns 0 for normal response of expected_normal_len bytes,
 * 1 for an exception, -1 for malformed/timeout. */
static int rtu_validate(modbus_t* ctx, const uint8_t* rsp, int rsp_len,
                        uint8_t expected_fc, int expected_normal_len) {
    /* Exception response is exactly 5 bytes: addr + (fc|0x80) + exc + CRC*2 */
    if (rsp_len >= 5 &&
        rtu_check_crc(rsp, 5) == 0 &&
        rsp[0] == (uint8_t)ctx->slave_id &&
        rsp[1] == (uint8_t)(expected_fc | 0x80)) {
        ctx->last_exception_code = rsp[2];
        return 1;
    }
    if (rsp_len < expected_normal_len) return -1;
    if (rtu_check_crc(rsp, rsp_len) != 0) return -1;
    if (rsp[0] != (uint8_t)ctx->slave_id || rsp[1] != expected_fc) return -1;
    return 0;
}

int modbus_read_registers(modbus_t* ctx, int addr, int nb, uint16_t* dest) {
    if (!ctx || !dest) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[256];
        int req_len = build_rtu_request(ctx, MODBUS_FC_READ_HOLDING_REGISTERS,
                                        (uint16_t)addr, (uint16_t)nb, req);
        int expected = nb * 2 + 5;
        int rsp_len = rtu_request(ctx, req, req_len, rsp, expected);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_READ_HOLDING_REGISTERS, expected) != 0) return -1;
        for (int i = 0; i < nb; i++)
            dest[i] = ((uint16_t)rsp[3 + i*2] << 8) | rsp[4 + i*2];
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_READ_HOLDING_REGISTERS, addr, nb, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_READ_HOLDING_REGISTERS) != 0) return -1;
    /* Normal PDU: FC (1) + byte_count (1) + nb*2 data bytes */
    if (rsp_len < 9 + nb * 2) return -1;
    int offset = 9;
    for (int i = 0; i < nb; i++) {
        dest[i] = (rsp[offset] << 8) | rsp[offset + 1];
        offset += 2;
    }
    return nb;
}

int modbus_write_register(modbus_t* ctx, int addr, const uint16_t value) {
    if (!ctx) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[8];
        int req_len = build_rtu_request(ctx, MODBUS_FC_WRITE_SINGLE_REGISTER,
                                        (uint16_t)addr, value, req);
        int rsp_len = rtu_request(ctx, req, req_len, rsp, 8);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_WRITE_SINGLE_REGISTER, 8) != 0) return -1;
        return 1;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, addr, value, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_WRITE_SINGLE_REGISTER) != 0) return -1;
    return 1;
}

int modbus_write_registers(modbus_t* ctx, int addr, int nb, const uint16_t* src) {
    if (!ctx || !src) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[256], rsp[8];
        int len = 0;
        req[len++] = (uint8_t)ctx->slave_id;
        req[len++] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
        req[len++] = (addr >> 8) & 0xFF; req[len++] = addr & 0xFF;
        req[len++] = (nb   >> 8) & 0xFF; req[len++] = nb   & 0xFF;
        req[len++] = (uint8_t)(nb * 2);
        for (int i = 0; i < nb; i++) {
            req[len++] = (src[i] >> 8) & 0xFF;
            req[len++] = src[i] & 0xFF;
        }
        uint16_t crc = crc16(req, len);
        req[len++] = crc & 0xFF; req[len++] = (crc >> 8) & 0xFF;
        int rsp_len = rtu_request(ctx, req, len, rsp, 8);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_WRITE_MULTIPLE_REGISTERS, 8) != 0) return -1;
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[256], rsp[260];
    int len = 0;
    req[len++] = (ctx->transaction_id >> 8) & 0xFF;
    req[len++] = ctx->transaction_id & 0xFF;
    ctx->transaction_id++;
    req[len++] = 0x00; req[len++] = 0x00;
    int length_pos = len;
    req[len++] = 0x00; req[len++] = 0x00;
    req[len++] = ctx->slave_id;
    req[len++] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    req[len++] = (addr >> 8) & 0xFF; req[len++] = addr & 0xFF;
    req[len++] = (nb   >> 8) & 0xFF; req[len++] = nb   & 0xFF;
    req[len++] = nb * 2;
    for (int i = 0; i < nb; i++) {
        req[len++] = (src[i] >> 8) & 0xFF;
        req[len++] = src[i] & 0xFF;
    }
    uint16_t length = len - 6;
    req[length_pos] = (length >> 8) & 0xFF; req[length_pos + 1] = length & 0xFF;
    if (send(ctx->socket, (const char*)req, len, 0) != len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_WRITE_MULTIPLE_REGISTERS) != 0) return -1;
    return nb;
}

int modbus_read_input_registers(modbus_t* ctx, int addr, int nb, uint16_t* dest) {
    if (!ctx || !dest) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[256];
        int req_len = build_rtu_request(ctx, MODBUS_FC_READ_INPUT_REGISTERS,
                                        (uint16_t)addr, (uint16_t)nb, req);
        int expected = nb * 2 + 5;
        int rsp_len = rtu_request(ctx, req, req_len, rsp, expected);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_READ_INPUT_REGISTERS, expected) != 0) return -1;
        for (int i = 0; i < nb; i++)
            dest[i] = ((uint16_t)rsp[3 + i*2] << 8) | rsp[4 + i*2];
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_READ_INPUT_REGISTERS, addr, nb, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_READ_INPUT_REGISTERS) != 0) return -1;
    if (rsp_len < 9 + nb * 2) return -1;
    int offset = 9;
    for (int i = 0; i < nb; i++) {
        dest[i] = (rsp[offset] << 8) | rsp[offset + 1];
        offset += 2;
    }
    return nb;
}

int modbus_read_bits(modbus_t* ctx, int addr, int nb, uint8_t* dest) {
    if (!ctx || !dest) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[256];
        int req_len = build_rtu_request(ctx, MODBUS_FC_READ_COILS,
                                        (uint16_t)addr, (uint16_t)nb, req);
        int expected = (nb + 7) / 8 + 5;
        int rsp_len = rtu_request(ctx, req, req_len, rsp, expected);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_READ_COILS, expected) != 0) return -1;
        for (int i = 0; i < nb; i++)
            dest[i] = (rsp[3 + i/8] >> (i % 8)) & 0x01;
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_READ_COILS, addr, nb, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_READ_COILS) != 0) return -1;
    int byte_count = (nb + 7) / 8;
    if (rsp_len < 9 + byte_count) return -1;
    int offset = 9;
    for (int i = 0; i < nb; i++) {
        dest[i] = (rsp[offset + i/8] >> (i % 8)) & 0x01;
    }
    return nb;
}

int modbus_write_bit(modbus_t* ctx, int addr, int status) {
    if (!ctx) return -1;
    ctx->last_exception_code = 0;
    uint16_t value = status ? 0xFF00 : 0x0000;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[8];
        int req_len = build_rtu_request(ctx, MODBUS_FC_WRITE_SINGLE_COIL,
                                        (uint16_t)addr, value, req);
        int rsp_len = rtu_request(ctx, req, req_len, rsp, 8);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_WRITE_SINGLE_COIL, 8) != 0) return -1;
        return 1;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_WRITE_SINGLE_COIL, addr, value, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_WRITE_SINGLE_COIL) != 0) return -1;
    return 1;
}

int modbus_read_input_bits(modbus_t* ctx, int addr, int nb, uint8_t* dest) {
    if (!ctx || !dest) return -1;
    ctx->last_exception_code = 0;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[8], rsp[256];
        int req_len = build_rtu_request(ctx, MODBUS_FC_READ_DISCRETE_INPUTS,
                                        (uint16_t)addr, (uint16_t)nb, req);
        int expected = (nb + 7) / 8 + 5;
        int rsp_len = rtu_request(ctx, req, req_len, rsp, expected);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_READ_DISCRETE_INPUTS, expected) != 0) return -1;
        for (int i = 0; i < nb; i++)
            dest[i] = (rsp[3 + i/8] >> (i % 8)) & 0x01;
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[12], rsp[260];
    int req_len = build_tcp_request(ctx, MODBUS_FC_READ_DISCRETE_INPUTS, addr, nb, req);
    if (send(ctx->socket, (const char*)req, req_len, 0) != req_len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_READ_DISCRETE_INPUTS) != 0) return -1;
    int byte_count = (nb + 7) / 8;
    if (rsp_len < 9 + byte_count) return -1;
    int offset = 9;
    for (int i = 0; i < nb; i++) {
        dest[i] = (rsp[offset + i/8] >> (i % 8)) & 0x01;
    }
    return nb;
}

int modbus_write_bits(modbus_t* ctx, int addr, int nb, const uint8_t* src) {
    if (!ctx || !src) return -1;
    ctx->last_exception_code = 0;
    int byte_count = (nb + 7) / 8;
    if (ctx->type == MODBUS_RTU) {
        uint8_t req[256], rsp[8];
        int len = 0;
        req[len++] = (uint8_t)ctx->slave_id;
        req[len++] = MODBUS_FC_WRITE_MULTIPLE_COILS;
        req[len++] = (addr >> 8) & 0xFF; req[len++] = addr & 0xFF;
        req[len++] = (nb   >> 8) & 0xFF; req[len++] = nb   & 0xFF;
        req[len++] = (uint8_t)byte_count;
        for (int i = 0; i < byte_count; i++) {
            uint8_t byte = 0;
            for (int j = 0; j < 8 && (i*8+j) < nb; j++)
                if (src[i*8+j]) byte |= (1 << j);
            req[len++] = byte;
        }
        uint16_t crc = crc16(req, len);
        req[len++] = crc & 0xFF; req[len++] = (crc >> 8) & 0xFF;
        int rsp_len = rtu_request(ctx, req, len, rsp, 8);
        if (rtu_validate(ctx, rsp, rsp_len, MODBUS_FC_WRITE_MULTIPLE_COILS, 8) != 0) return -1;
        return nb;
    }
    if (ctx->socket == INVALID_SOCKET_VALUE) return -1;
    uint8_t req[256], rsp[260];
    int len = 0;
    req[len++] = (ctx->transaction_id >> 8) & 0xFF;
    req[len++] = ctx->transaction_id & 0xFF;
    ctx->transaction_id++;
    req[len++] = 0x00; req[len++] = 0x00;
    int length_pos = len;
    req[len++] = 0x00; req[len++] = 0x00;
    req[len++] = ctx->slave_id;
    req[len++] = MODBUS_FC_WRITE_MULTIPLE_COILS;
    req[len++] = (addr >> 8) & 0xFF; req[len++] = addr & 0xFF;
    req[len++] = (nb   >> 8) & 0xFF; req[len++] = nb   & 0xFF;
    req[len++] = byte_count;
    for (int i = 0; i < byte_count; i++) {
        uint8_t byte = 0;
        for (int j = 0; j < 8 && (i*8+j) < nb; j++)
            if (src[i*8+j]) byte |= (1 << j);
        req[len++] = byte;
    }
    uint16_t length = len - 6;
    req[length_pos] = (length >> 8) & 0xFF; req[length_pos + 1] = length & 0xFF;
    if (send(ctx->socket, (const char*)req, len, 0) != len) return -1;
    int rsp_len = tcp_recv_response(ctx, rsp, sizeof(rsp));
    if (rsp_len < 0) return -1;
    if (check_exception_pdu(ctx, &rsp[7], rsp_len - 7, MODBUS_FC_WRITE_MULTIPLE_COILS) != 0) return -1;
    return nb;
}

/* ══ Raw serial I/O (for gateway use) ════════════════════════════════ */

int modbus_send_raw(modbus_t* ctx, const uint8_t* buf, int len) {
    if (!ctx || ctx->type != MODBUS_RTU || !buf || len <= 0) return -1;
#ifdef _WIN32
    DWORD written = 0;
    /* Purge both TX and RX: clears any echo/stale bytes before sending */
    PurgeComm(ctx->serial_handle, PURGE_TXCLEAR | PURGE_RXCLEAR);
    if (!WriteFile(ctx->serial_handle, buf, (DWORD)len, &written, NULL)) return -1;
    return (int)written;
#else
    /* Flush pending I/O so we start clean */
    tcflush(ctx->serial_fd, TCIOFLUSH);
    return (int)write(ctx->serial_fd, buf, len);
#endif
}

int modbus_recv_raw(modbus_t* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    if (!ctx || ctx->type != MODBUS_RTU || !buf || maxlen <= 0) return -1;
    int total = 0;
#ifdef _WIN32
    /* Do NOT purge RX here — the response may already be arriving.
       Use COMMTIMEOUTS: wait up to timeout_ms for first byte,
       then allow up to 50 ms inter-character gap to catch the rest. */
    COMMTIMEOUTS ct;
    GetCommTimeouts(ctx->serial_handle, &ct);
    COMMTIMEOUTS nct;
    nct.ReadIntervalTimeout         = 50;         /* 50 ms gap ends frame */
    nct.ReadTotalTimeoutMultiplier  = 0;
    nct.ReadTotalTimeoutConstant    = timeout_ms; /* first-byte wait */
    nct.WriteTotalTimeoutMultiplier = 0;
    nct.WriteTotalTimeoutConstant   = 0;
    SetCommTimeouts(ctx->serial_handle, &nct);
    DWORD rd = 0;
    while (total < maxlen) {
        rd = 0;
        if (!ReadFile(ctx->serial_handle, buf + total, maxlen - total, &rd, NULL)) break;
        if (rd == 0) break; /* timeout gap — end of frame */
        total += (int)rd;
        /* After first bytes arrive reduce total timeout to just the gap */
        if (total > 0 && nct.ReadTotalTimeoutConstant != 50) {
            nct.ReadTotalTimeoutConstant = 50;
            SetCommTimeouts(ctx->serial_handle, &nct);
        }
    }
    SetCommTimeouts(ctx->serial_handle, &ct);
#else
    while (total < maxlen) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ctx->serial_fd, &fds);
        int wait_ms = (total == 0) ? timeout_ms : 50; /* inter-frame gap */
        tv.tv_sec  = wait_ms / 1000;
        tv.tv_usec = (wait_ms % 1000) * 1000;
        if (select(ctx->serial_fd + 1, &fds, NULL, NULL, &tv) <= 0) break;
        int rd = (int)read(ctx->serial_fd, buf + total, maxlen - total);
        if (rd <= 0) break;
        total += rd;
    }
#endif
    return total;
}

const char* modbus_strerror(int errnum) {
#ifdef _WIN32
    static char buf[256];
    if (errnum == EACCES || errnum == ENOENT || errnum == EIO) {
        /* RTU serial errors mapped to errno — use strerror */
        return strerror(errnum);
    }
    /* TCP socket errors — use FormatMessage with WSA error */
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, WSAGetLastError(), 0, buf, sizeof(buf), NULL);
    return buf;
#else
    return strerror(errnum);
#endif
}

int modbus_get_last_exception(modbus_t* ctx) {
    return ctx ? ctx->last_exception_code : 0;
}

const char* modbus_exception_string(int code) {
    switch (code) {
        case 1:  return "Illegal function";
        case 2:  return "Illegal data address";
        case 3:  return "Illegal data value";
        case 4:  return "Slave device failure";
        case 5:  return "Acknowledge";
        case 6:  return "Slave device busy";
        case 7:  return "Negative acknowledge";
        case 8:  return "Memory parity error";
        case 10: return "Gateway path unavailable";
        case 11: return "Gateway target device failed to respond";
        default: return "Unknown exception";
    }
}
