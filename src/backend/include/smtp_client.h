#ifndef SMTP_CLIENT_H
#define SMTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// SMTP configuration structure
typedef struct {
    char server[128];           // SMTP server address
    int port;                   // SMTP port (usually 587 for TLS, 25 for plain)
    char username[128];         // SMTP username
    char password[128];         // SMTP password
    char from_email[128];       // Sender email address
    char from_name[128];        // Sender name
    char to_email[512];         // Recipient email (can be multiple, comma-separated)
    bool use_tls;               // Use TLS/STARTTLS
    bool enabled;               // Email notifications enabled
} smtp_config_t;

// Email message structure
typedef struct {
    char subject[256];
    char body[2048];
    char to[512];               // Override recipients (optional)
} email_message_t;

// Initialize SMTP client with configuration
bool smtp_init(smtp_config_t* config);

// Send email
bool smtp_send_email(const email_message_t* message);

// Get current configuration
smtp_config_t* smtp_get_config(void);

// Set configuration
void smtp_set_config(const smtp_config_t* config);

// Test connection
bool smtp_test_connection(void);

// Cleanup
void smtp_cleanup(void);

#endif // SMTP_CLIENT_H
