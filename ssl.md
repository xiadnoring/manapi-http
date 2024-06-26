LLM served by Perplexity Labs
To create an HTTP server-side with SSL using C++ and OpenSSL on Linux, you can follow these steps:

    Include Necessary Headers:

    cpp
    #include <iostream>
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>

Create an SSL Context and Configure It:

cpp
SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
if (!ctx) {
ERR_print_errors_fp(stderr);
exit(EXIT_FAILURE);
}

// Load the server certificate and private key
if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
ERR_print_errors_fp(stderr);
exit(EXIT_FAILURE);
}

if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
ERR_print_errors_fp(stderr);
exit(EXIT_FAILURE);
}

Create a Server Socket:

cpp
int server_fd, new_socket;
struct sockaddr_in address;
int opt = 1;
int addrlen = sizeof(address);
char buffer[1024] = {0};

// Create the server socket
if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
perror("socket failed");
exit(EXIT_FAILURE);
}

// Set address and port number for the server
address.sin_family = AF_INET;
address.sin_addr.s_addr = INADDR_ANY;
address.sin_port = htons( 8080 );

// Bind the socket to the address and port
if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
perror("bind failed");
exit(EXIT_FAILURE);
}

// Listen for incoming connections
if (listen(server_fd, 3) < 0) {
perror("listen");
exit(EXIT_FAILURE);
}

// Accept incoming connections
if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
perror("accept");
exit(EXIT_FAILURE);
}

Create an SSL Object and Perform the SSL Handshake:

cpp
SSL* ssl;
ssl = SSL_new(ctx);
SSL_set_fd(ssl, new_socket);

if (SSL_accept(ssl) <= 0) {
ERR_print_errors_fp(stderr);
}

Send the HTTP Response:

cpp
const char* response = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"\r\n"
"<html><body><h1>Hello, World!</h1></body></html>";

SSL_write(ssl, response, strlen(response));

Clean Up:

cpp
SSL_shutdown(ssl);
SSL_free(ssl);
close(new_socket);
SSL_CTX_free(ctx);

Here is the complete code:

cpp
#include <iostream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main() {
SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
if (!ctx) {
ERR_print_errors_fp(stderr);
exit(EXIT_FAILURE);
}

    // Load the server certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // Create the server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set address and port number for the server
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( 8080 );

    // Bind the socket to the address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Accept incoming connections
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    SSL* ssl;
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, new_socket);

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    }

    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "\r\n"
                           "<html><body><h1>Hello, World!</h1></body></html>";

    SSL_write(ssl, response, strlen(response));

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(new_socket);
    SSL_CTX_free(ctx);

    return 0;
}

This code sets up an HTTPS server that listens on port 8080 and responds with a simple HTML page when a client connects. Make sure to replace "server.crt" and "server.key" with your actual certificate and private key files.