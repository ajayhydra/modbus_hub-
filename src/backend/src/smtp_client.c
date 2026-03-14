#include "smtp_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static smtp_config_t g_smtp_config = {0};
static bool g_smtp_initialized = false;

// Base64 encoding table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 encode
static void base64_encode(const char* input, int length, char* output) {
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    while (length--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                output[j++] = base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (int k = i; k < 3; k++)
            char_array_3[k] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int k = 0; k < i + 1; k++)
            output[j++] = base64_chars[char_array_4[k]];
        
        while (i++ < 3)
            output[j++] = '=';
    }
    output[j] = '\0';
}

// Send command and get response
static bool smtp_send_receive(int sock, const char* cmd, char* response, int resp_size, int expected_code) {
    char buffer[1024];
    ssize_t len;
    
    // Send command
    if (cmd && strlen(cmd) > 0) {
        if (send(sock, cmd, strlen(cmd), 0) < 0) {
            return false;
        }
    }
    
    // Receive response
    memset(buffer, 0, sizeof(buffer));
    len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        return false;
    }
    buffer[len] = '\0';
    
    if (response && resp_size > 0) {
        strncpy(response, buffer, resp_size - 1);
        response[resp_size - 1] = '\0';
    }
    
    // Check response code
    if (expected_code > 0) {
        int code = 0;
        sscanf(buffer, "%d", &code);
        if (code != expected_code) {
            return false;
        }
    }
    
    return true;
}

bool smtp_init(smtp_config_t* config) {
    if (config) {
        memcpy(&g_smtp_config, config, sizeof(smtp_config_t));
    }
    g_smtp_initialized = true;
    return true;
}

void smtp_set_config(const smtp_config_t* config) {
    if (config) {
        memcpy(&g_smtp_config, config, sizeof(smtp_config_t));
    }
}

smtp_config_t* smtp_get_config(void) {
    return &g_smtp_config;
}

bool smtp_send_email(const email_message_t* message) {
    if (!g_smtp_initialized || !g_smtp_config.enabled) {
        return false;
    }
    
    if (!message || strlen(g_smtp_config.server) == 0 || strlen(g_smtp_config.from_email) == 0) {
        return false;
    }
    
    int sock;
    struct sockaddr_in server;
    struct hostent* he;
    char buffer[1024];
    char cmd[512];
    bool result = false;
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    // Set timeout (10 seconds)
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Resolve hostname
    he = gethostbyname(g_smtp_config.server);
    if (he == NULL) {
        close(sock);
        return false;
    }
    
    // Setup server address
    server.sin_family = AF_INET;
    server.sin_port = htons(g_smtp_config.port);
    memcpy(&server.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
    
    // Connect
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        close(sock);
        return false;
    }
    
    // Get server greeting (220)
    if (!smtp_send_receive(sock, NULL, buffer, sizeof(buffer), 220)) {
        goto cleanup;
    }
    
    // Send EHLO
    snprintf(cmd, sizeof(cmd), "EHLO localhost\r\n");
    if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 250)) {
        goto cleanup;
    }
    
    // AUTH LOGIN (if credentials provided)
    if (strlen(g_smtp_config.username) > 0) {
        snprintf(cmd, sizeof(cmd), "AUTH LOGIN\r\n");
        if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 334)) {
            goto cleanup;
        }
        
        // Send username (base64 encoded)
        char encoded[256];
        base64_encode(g_smtp_config.username, strlen(g_smtp_config.username), encoded);
        snprintf(cmd, sizeof(cmd), "%s\r\n", encoded);
        if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 334)) {
            goto cleanup;
        }
        
        // Send password (base64 encoded)
        base64_encode(g_smtp_config.password, strlen(g_smtp_config.password), encoded);
        snprintf(cmd, sizeof(cmd), "%s\r\n", encoded);
        if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 235)) {
            goto cleanup;
        }
    }
    
    // MAIL FROM
    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>\r\n", g_smtp_config.from_email);
    if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 250)) {
        goto cleanup;
    }
    
    // RCPT TO
    const char* to_email = (message->to[0] != '\0') ? message->to : g_smtp_config.to_email;
    snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>\r\n", to_email);
    if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 250)) {
        goto cleanup;
    }
    
    // DATA
    snprintf(cmd, sizeof(cmd), "DATA\r\n");
    if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 354)) {
        goto cleanup;
    }
    
    // Message headers and body
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S %z", tm_info);
    
    snprintf(cmd, sizeof(cmd),
             "From: %s <%s>\r\n"
             "To: %s\r\n"
             "Subject: %s\r\n"
             "Date: %s\r\n"
             "\r\n"
             "%s\r\n"
             ".\r\n",
             g_smtp_config.from_name, g_smtp_config.from_email,
             to_email,
             message->subject,
             date_str,
             message->body);
    
    if (!smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 250)) {
        goto cleanup;
    }
    
    result = true;
    
cleanup:
    // QUIT
    snprintf(cmd, sizeof(cmd), "QUIT\r\n");
    smtp_send_receive(sock, cmd, buffer, sizeof(buffer), 221);
    
    close(sock);
    return result;
}

bool smtp_test_connection(void) {
    if (!g_smtp_initialized || strlen(g_smtp_config.server) == 0) {
        return false;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    struct hostent* he = gethostbyname(g_smtp_config.server);
    if (he == NULL) {
        close(sock);
        return false;
    }
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(g_smtp_config.port);
    memcpy(&server.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
    
    bool result = (connect(sock, (struct sockaddr*)&server, sizeof(server)) >= 0);
    close(sock);
    
    return result;
}

void smtp_cleanup(void) {
    g_smtp_initialized = false;
}
