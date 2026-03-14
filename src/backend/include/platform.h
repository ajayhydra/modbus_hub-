#ifndef PLATFORM_H
#define PLATFORM_H

// Platform detection
#ifdef _WIN32
    #define PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    typedef SOCKET socket_t;
    typedef HANDLE thread_t;
    typedef DWORD thread_return_t;
    #define THREAD_CALL WINAPI
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    
#else
    #define PLATFORM_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <errno.h>
    #include <fcntl.h>
    
    typedef int socket_t;
    typedef pthread_t thread_t;
    typedef void* thread_return_t;
    #define THREAD_CALL
    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_ERROR_VALUE -1
    #define closesocket close
    #define Sleep(ms) usleep((ms) * 1000)
    #define SOCKET_ERROR -1
#endif

// Cross-platform networking functions
int platform_init_networking(void);
void platform_cleanup_networking(void);
int platform_close_socket(socket_t sock);

// Cross-platform threading functions
int platform_create_thread(thread_t *thread, thread_return_t (THREAD_CALL *func)(void*), void *arg);
void platform_join_thread(thread_t thread);

// Serial port helpers
const char* platform_get_serial_port_name(int port_number);

#endif // PLATFORM_H
