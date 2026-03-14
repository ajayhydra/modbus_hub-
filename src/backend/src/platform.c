#include "platform.h"
#include <string.h>
#include <stdio.h>

// Initialize networking subsystem
int platform_init_networking(void) {
#ifdef PLATFORM_WINDOWS
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    return 0; // No initialization needed on Linux
#endif
}

// Cleanup networking subsystem
void platform_cleanup_networking(void) {
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
}

// Close a socket
int platform_close_socket(socket_t sock) {
#ifdef PLATFORM_WINDOWS
    return closesocket(sock);
#else
    return close(sock);
#endif
}

// Create a thread
int platform_create_thread(thread_t *thread, thread_return_t (THREAD_CALL *func)(void*), void *arg) {
#ifdef PLATFORM_WINDOWS
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
#else
    return pthread_create(thread, NULL, func, arg);
#endif
}

// Wait for thread to finish
void platform_join_thread(thread_t thread) {
#ifdef PLATFORM_WINDOWS
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
}

// Get serial port name (COM1 on Windows, /dev/ttyUSB0 on Linux)
const char* platform_get_serial_port_name(int port_number) {
    static char port_name[64];
#ifdef PLATFORM_WINDOWS
    snprintf(port_name, sizeof(port_name), "COM%d", port_number);
#else
    // Linux typically uses /dev/ttyUSB0, /dev/ttyS0, etc.
    snprintf(port_name, sizeof(port_name), "/dev/ttyUSB%d", port_number);
#endif
    return port_name;
}
