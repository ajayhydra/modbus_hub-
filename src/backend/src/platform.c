#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PLATFORM_WINDOWS
struct platform_mutex { CRITICAL_SECTION cs; };
#else
struct platform_mutex { pthread_mutex_t m; };
#endif

platform_mutex_t* platform_mutex_create(void) {
    platform_mutex_t* m = (platform_mutex_t*)malloc(sizeof(platform_mutex_t));
    if (!m) return NULL;
#ifdef PLATFORM_WINDOWS
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &attr);
    pthread_mutexattr_destroy(&attr);
#endif
    return m;
}

void platform_mutex_destroy(platform_mutex_t* m) {
    if (!m) return;
#ifdef PLATFORM_WINDOWS
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->m);
#endif
    free(m);
}

void platform_mutex_lock(platform_mutex_t* m) {
    if (!m) return;
#ifdef PLATFORM_WINDOWS
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
}

void platform_mutex_unlock(platform_mutex_t* m) {
    if (!m) return;
#ifdef PLATFORM_WINDOWS
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
}

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
