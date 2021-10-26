/*
webserver.c
A basic HTTP webserver that handles GET, HEAD, and POST requests.
It can support multiple connections and pipelining.
Author: Nimish Bhide (University of Colorado, Boulder)
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>                 
#include <strings.h>                
#include <unistd.h>                 
#include <sys/socket.h>             
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include<signal.h>


#define BUFF_SIZE (4096)        
#define DEF_HTTP_KEEPALIVE (10)     /* HTTP idle timeout default value */
#define THREAD_POOL_SIZE (50)
#define DEF_SERVER_PORT (8080)      /* Default server port */
#define SOCKET_BACKLOG (100)
#define MAX_FILEPATH_LENGTH (1024)
#define DEFAULT_PATH "./www"
#define DEFAULT_OBJECT "/index.html"

int server_socket;                  /* Stores server socket file descriptor */

int check(int n, char* err);
static void sig_handler(int signo);
char *get_content_type(char *path);
const char *get_ext(const char *fspec);
char *str_to_lower_case(char *str);
int is_valid_path(char *actual_file_path);
void *handle_new_connection(void *vargp);
int handle_http_head_request(char *file_uri, ssize_t *file_len, char *file_type);
char *handle_http_get_request(char *file_uri, ssize_t *file_len, char *file_type);
char *handle_http_post_request(char *file_uri, ssize_t *file_len, char *file_type, char *post_data);
void build_http_ok_response(char *resp_msg, char *version, ssize_t filesize, char *filetype, int conn_stat, char *buff);
void build_http_err_response(char *err_msg, char *version, int errsize, int conn_stat, char *buff);

// SIGINT Handler.
static void sig_handler(int signo)
{
    if((signo == SIGINT) || (signo == SIGTERM))
    {   
        write(STDERR_FILENO, "\nCaught SIGINT!. Closing server.\n", 33);
        close(server_socket);
        exit(EXIT_SUCCESS);
    }
}


/*
Takes in a string representing the filepath and 
returns the extension of the file.
Params -> filepath string
Return -> extension string
*/
char *getExt (char *fspec) 
{
    char *c = strrchr (fspec, '.');
    if (c == NULL)
        c = "";
    printf("File ext is: %s\n", c+1);
    return c+1;
}


/*
Takes in a string representing filepath and 
returns the MIME content type of the file.
Params -> filepath string
Return -> MIME type string
*/
char *get_content_type(char *path)
{   
    char *contype = malloc(sizeof(char)*11);
    if (strcmp(getExt(path), "html") == 0)
    {
        strcpy(contype, "text/html");
    }
    else if(strcmp(getExt(path), "txt") == 0)
    {
        strcpy(contype, "text/plain");
    }
    else if (strcmp(getExt(path), "jpg") == 0)
    {
        strcpy(contype, "image/jpg");
    }
    else if(strcmp(getExt(path), "png") == 0)
    {
        strcpy(contype, "image/png");
    }
    else if(strcmp(getExt(path), "gif") == 0)
    {
        strcpy(contype, "image/gif");
    }
    else if(strcmp(getExt(path), "css") == 0)
    {
        strcpy(contype, "text/css");
    }

    return contype;
}


// Guard function to look for failures.
int check(int n, char* err)
{
    if (n == -1)
    {
        perror(err);
        exit(1);
    }
    return n;
}


/*
Takes in a string and returns the 
lowercase version for it.
Params ->  string
Return -> lowercase string
*/
char *str_to_lower_case(char *str)
{   
    for (size_t i = 0; str[i]; i++)
    {
        str[i] = tolower(str[i]);
    }
    return str;
}


/*
Takes in a string representing filepath and 
checks whether the file exists.
Params -> filepath string
Return -> 1 if file exists; 0 if it doesn't.
*/
int is_valid_path(char *actual_file_path)
{   
    if (access(actual_file_path, F_OK) != 0)
    {   
        printf("Invalid filepath.\n");
        return 0;
    }
    else
    {   
        return 1;
    }
}


/*
Combines HTTP headers and body and writes
to a a buffer. Output differs based on value
of conn_stat flag representing keep-alive.
Return -> buffer containing message to send back.
*/
void build_http_ok_response(char *resp_msg, char *version, ssize_t filesize, char *filetype, int conn_stat, char *buff)
{  
    if (resp_msg == NULL)
        sprintf(buff, "%s 200 OK\r\n""Content-Type: %s\r\n""Connection: %s\r\n""Content-Length: %ld\r\n\r\n""\r\n", 
                version, 
                filetype, 
                conn_stat == 1 ? "Keep-alive": "Close", 
                filesize);

    sprintf(buff, "%s 200 OK\r\n""Content-Type: %s\r\n""Connection: %s\r\n""Content-Length: %ld\r\n\r\n", 
            version, 
            filetype, 
            conn_stat == 1 ? "Keep-alive": "Close", 
            filesize);
}


/*
Forms an HTTP 500 error response.
Output differs based on value of conn_stat 
flag representing keep-alive.
Return -> buffer containing message to send back.
*/
void build_http_err_response(char *err_msg, char *version, int errsize, int conn_stat, char *buff)
{   
    sprintf(buff, "%s 500 Internal Server Error\r\n""Content-Type: text/html\r\n""Connection: %s\r\n""Content-Length: %d\r\n\r\n""%s\r\n", 
            version, 
            conn_stat == 1? "Keep-alive": "Close", 
            errsize, 
            err_msg);
}

/*
Handles HTTP HEAD request.
Return -> 1 if file is valid; 
          0 if not.
*/
int handle_http_head_request(char *file_uri, ssize_t *file_len, char *file_type)
{   
    struct stat st;
    
    // Form filepath to check.
    char path[MAX_FILEPATH_LENGTH + 1];
    strcpy(path, DEFAULT_PATH);
    if (strcmp(file_uri, "/") == 0)
    {
        strcat(path, DEFAULT_OBJECT);
    }
    else
    {
        strcat(path, file_uri);
    }

    int validfile = is_valid_path(path);
    if (validfile == 1)
    {
        //get file type and size.
        stat(path, &st);
        *(file_len) = st.st_size;
        strcpy(file_type, get_content_type(path));
        return 1;
    }
    else
    {
        // return error response.
        *(file_len) = 0;
        file_type = NULL;
        return 0;
    }
}


/*
Handles HTTP GET request.
Return -> string buff containing file if file exists.
          NULL if file does not exist.
*/
char *handle_http_get_request(char *file_uri, ssize_t *file_len, char *file_type)
{
    printf("Came to the get req handler.\n");
    struct stat st;
    
    // Form filepath to check.
    char path[MAX_FILEPATH_LENGTH + 1];
    strcpy(path, DEFAULT_PATH); // TODO: Modify later to use strncpy

    if (strcmp(file_uri, "/") == 0)
    {
        strcat(path, DEFAULT_OBJECT);
    }
    else
    {
        strcat(path, file_uri);
    }

    int validfile = is_valid_path(path);
    if (validfile == 1)
    {
        //get file type and size.
        stat(path, &st);
        *(file_len) = st.st_size;
        char *buf = (char *)malloc(sizeof(char)*(st.st_size));
        strcpy(file_type, get_content_type(path));
        FILE *file_ptr;
        file_ptr = fopen(path, "rb");
        fread(buf, 1, *(file_len), file_ptr);
        return buf;
    }
    else
    {
        // return error response.
        *(file_len) = 0;
        file_type = NULL;
        return NULL;
    }
}


/*
Handles HTTP POST request.
Return -> string buff containing file if file exists.
          NULL if file does not exist.
*/
char *handle_http_post_request(char *file_uri, ssize_t *file_len, char *file_type, char *post_data)
{
    printf("Came to the post req handler.\n");
    struct stat st;
    
    // Form filepath to check.
    char path[MAX_FILEPATH_LENGTH + 1];
    strcpy(path, DEFAULT_PATH);

    if (strcmp(file_uri, "/") == 0)
    {
        strcat(path, DEFAULT_OBJECT);
    }
    else
    {
        strcat(path, file_uri);
    }

    int validfile = is_valid_path(path);
    if (validfile == 1)
    {
        stat(path, &st);
        char *buf = (char *)malloc(sizeof(char)*(st.st_size));
        strcpy(file_type, get_content_type(path));
        FILE *file_ptr;
        file_ptr = fopen(path, "rb");
        fread(buf, 1, st.st_size, file_ptr);
        printf("Actual post file: %s\n", buf);

        // Prepend post data.
        char *post_html = malloc(sizeof(char)*(strlen(post_data)+st.st_size+32+1));
        sprintf(post_html, "<html><body><pre><h1>%s</h1></pre>%s", post_data, buf);
        *(file_len) = strlen(post_html);
        free(buf);
        return post_html;
    }
    else
    {
        // return error response.
        *(file_len) = 0;
        file_type = NULL;
        return NULL;
    }
}


// main()
int main(int argc, char **argv)
{   
    // Setting up signal handler.
    if (signal(SIGINT, sig_handler) == SIG_ERR)
        exit(EXIT_FAILURE);

    // Check for invalid input from CLI.
    if ((argc != 2) || (atoi(argv[1]) < 5000))
    {   
        // Print out error message explaining correct way to input.
        printf("Invalid input/port.\n");
        printf("Usage --> ./[%s] [Port Number]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int srv_port = atoi(argv[1]);           // Store server port received in input.
    struct sockaddr_in srv_addr;            // Server address.
    int srv_addrlen = sizeof(srv_addr);     // Server address length.

    int *client_socket;                      // Store client socket file descriptor.
    int optval = 1;

    pthread_t thread_id;

    // Create TCP socket.
    server_socket = check(socket(AF_INET, SOCK_STREAM, 0), "could not create TCP listening socket");

    // Eliminates "Address already in use" error from bind.
    check(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                        (const void *)&optval, sizeof(int)), "setsockopt(SO_REUSEADDR) failed");

    // Initialise the address struct.
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(atoi(argv[1]));

    // Bind the socket.
    check(bind(server_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr)), "bind failed");
    check(listen(server_socket, SOCKET_BACKLOG), "could not listen");

    while (1)
    {   
        printf("Waiting for a connection on port %d. \r\n", srv_port);

        client_socket = malloc(sizeof(int));
        check(*client_socket = 
                accept(server_socket, (struct sockaddr *)&srv_addr, (socklen_t *)&srv_addrlen), 
                "accept failed");
        printf("Connected\r\n");
        pthread_create(&thread_id, NULL, handle_new_connection, client_socket);  // Spawn a new thread to handle request.
    }
    // Exit process after closing the socket.
    close(server_socket);
    exit(EXIT_SUCCESS);
}


void *handle_new_connection(void *vargp)
{   
    int client_socket = *((int *)vargp);
    char recv_buffer[BUFF_SIZE];
    char send_buffer[BUFF_SIZE];
    ssize_t bytes_read;
    char filepath[MAX_FILEPATH_LENGTH + 1];
    struct timeval timeout;
    char http_method[10];
    char http_version[10];
    char next_header_val[2][25]; // Idx 0 is Host, Idx 1 is Connection.
    char next_header_key[2][25]; // Idx 0 is Host, Idx 1 is Connection.
    int keep_alive = 0;
    char *keep_alive_str = "keep-alive";
    char *conn_close_str = "close";
    char *error_msg = "<!DOCTYPE html><html><title>Invalid Request</title>""<pre><h1>500 Internal Server Error</h1></pre>""</html>\r\n";
    memset(recv_buffer, 0, sizeof(recv_buffer));

    while ((bytes_read = recv(client_socket, recv_buffer, sizeof(recv_buffer), 0)) > 0)
    {   
        memset(http_method, 0, sizeof(http_method));
        memset(http_version, 0, sizeof(http_version));
        memset(next_header_key, 0, sizeof(next_header_key));
        memset(next_header_val, 0, sizeof(next_header_val));

        printf("Bytes read: %zd\n", bytes_read);

        // Parse HTTP request.
        char *context = NULL;
        char *token = strtok_r(recv_buffer, "\r\n", &context);
        int line_count = 1;
        char *post_data = NULL;
        while (token != NULL)
        {   
            if (line_count == 1)
            {
                sscanf(token, "%s %s %s", http_method, filepath, http_version);
            }
            else if (line_count == 2 || line_count == 3)
            {   
                sscanf(token, "%s %s", next_header_key[line_count-2], next_header_val[line_count-2]);
            }
            else if (strcmp(http_method, "POST") == 0 && line_count == 4)
            {   
                ssize_t toklen = strlen(token);
                post_data = malloc(sizeof(char)*toklen);
                sscanf(token, "%s", post_data);
            }
            token = strtok_r(NULL, "\r\n", &context);
            line_count++;
        }

        memset(recv_buffer, 0, sizeof(recv_buffer));                      // Reset receive buffer.

        char *lowercase_connection_val = str_to_lower_case(next_header_val[1]);
        if (strcmp(lowercase_connection_val, keep_alive_str) == 0)
        {   
            keep_alive = 1;
            timeout.tv_sec = DEF_HTTP_KEEPALIVE;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(struct timeval));
        }
        else if (strcmp(lowercase_connection_val, conn_close_str) == 0)
        {
            keep_alive = 0;
            timeout.tv_sec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(struct timeval));
        }

        // Check for invalid http method and version.
        // if method is not head, get, or post, return error
        // if version is not HTTP/1.0 or HTTP/1.1, return error.
        if ((strcmp(http_method, "GET") != 0) && 
            (strcmp(http_method, "POST") != 0) && 
            (strcmp(http_method, "HEAD") != 0))
        {   
            printf("Invalid HTTP method.\n");
            memset(send_buffer, 0, sizeof(send_buffer));

            // Send error response and keep connection alive/kill.
            if (keep_alive == 1)
            {
                build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                continue;
            }
            else
            {
                build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 0, send_buffer);
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                printf("Closing HTTP connection.\n");
                close(client_socket);
                pthread_detach(pthread_self());
                free(vargp);
                return NULL;
            }
        }

        else if ((strcmp(http_version, "HTTP/1.0") != 0) && 
            (strcmp(http_version, "HTTP/1.1") != 0))
        {
            printf("Invalid HTTP version.\n");
            memset(send_buffer, 0, sizeof(send_buffer));
            
            // Send error response and keep connection alive/kill.
            if (keep_alive == 1)
            {
                build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                continue;
            }
            else
            {
                build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 0, send_buffer);
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                printf("Closing HTTP connection.\n");
                pthread_detach(pthread_self());
                free(vargp);
                close(client_socket);
                return NULL;
            }
        }
        

        else if (strcmp(http_method, "HEAD") == 0)
        {   
            //call function that handles HEAD.
            memset(send_buffer, 0, sizeof(send_buffer));
            ssize_t content_len;
            char content_type[25] = "";

            if (keep_alive == 1)
            {   
                if (handle_http_head_request(filepath, &content_len, content_type) == 1)
                {   
                    build_http_ok_response(NULL, http_version, content_len, content_type, 1, send_buffer);
                }
                else
                {
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                continue;
            }
            else
            {   
                if (handle_http_head_request(filepath, &content_len, content_type) == 1)
                {
                    build_http_ok_response(NULL, http_version, content_len, content_type, 0, send_buffer);
                }
                else
                {
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 0, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                pthread_detach(pthread_self());
                free(vargp);
                close(client_socket);
                return NULL;
            }
        }

        else if (strcmp(http_method, "GET") == 0)
        {
            //call func to handle GET.
            memset(send_buffer, 0, sizeof(send_buffer));
            ssize_t content_len;
            char content_type[25] = "";
            char *file_contents;
            if (keep_alive == 1)
            {   
                if ((file_contents = handle_http_get_request(filepath, &content_len, content_type)) == NULL)
                {   
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                }
                else
                {   
                    build_http_ok_response(file_contents, http_version, content_len, content_type, 1, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                send(client_socket, file_contents, content_len, 0);
                free(file_contents);
                continue;
            }
            else
            {   
                if ((file_contents = handle_http_get_request(filepath, &content_len, content_type)) == NULL)
                {
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 0, send_buffer);
                }
                else
                {   
                    build_http_ok_response(file_contents, http_version, content_len, content_type, 0, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                send(client_socket, file_contents, content_len, 0);
                free(file_contents);
                pthread_detach(pthread_self());
                free(vargp);
                close(client_socket);
                return NULL;
            }
        }

        else if (strcmp(http_method, "POST") == 0)
        {
            //call func to handle POST.
            memset(send_buffer, 0, sizeof(send_buffer));
            ssize_t content_len;
            char content_type[25] = "";
            char *post_contents;

            if (keep_alive == 1)
            {
                if ((post_contents = handle_http_post_request(filepath, &content_len, content_type, post_data)) == NULL)
                {
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                }
                else
                {
                    build_http_ok_response(post_contents, http_version, content_len, content_type, 0, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                send(client_socket, post_contents, content_len, 0);
                free(post_contents);
                continue;
            }
            else
            {
                if ((post_contents = handle_http_post_request(filepath, &content_len, content_type, post_data)) == NULL)
                {
                    build_http_err_response(error_msg, "HTTP/1.1", strlen(error_msg), 1, send_buffer);
                }
                else
                {
                    build_http_ok_response(post_contents, http_version, content_len, content_type, 0, send_buffer);
                }
                send(client_socket, send_buffer, strlen(send_buffer), 0);
                send(client_socket, post_contents, content_len, 0);
                free(post_contents);
                pthread_detach(pthread_self());
                free(vargp);
                close(client_socket);
                return NULL;
            }
        }
    }
    
    if (bytes_read < 0 && errno == EWOULDBLOCK)
    {   
        pthread_detach(pthread_self());
        free(vargp);
        close(client_socket);
    }
    return NULL;
}

