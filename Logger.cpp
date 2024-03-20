/********************************** SETUP LIBRARIES BEGINS ********************************/

#include <iostream>
#include <unistd.h>
#include <atomic>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <time.h>
#include <queue>
#include <iterator>
#include "Logger.h"

/********************************** SETUP LIBRARIES ENDS **********************************/





/********************************** SETUP SECTION BEGINS **********************************/

#define PORT_SERVER 8080
#define PORT_LOGGER 9090
#define SIZE_BUF 1024

const char ADDRESS_SERVER[] = "127.0.0.1";
const char ADDRESS_LOGGER[] = "127.0.0.1";

// connections - server - address, port, socket
struct sockaddr_in addr_server;
int fd_socket = -1;

// connections - logger
struct sockaddr_in addr_logger;

// communication - message, buffer
char message[] = "";
char buffer[SIZE_BUF];

// log level - defaults to DEBUG mode
LOG_LEVEL log_level_current = DEBUG;

// use atomic to avoid race condition
std::atomic<bool> is_running = true;

// for threads
std::thread recv_thread;

// for mutex, we need to use thread methods but not std::mutex
pthread_mutex_t mutex_log; 
pthread_t thread_receive;

/********************************** SETUP SECTION ENDS ***********************************/





/********************************** FUNCTIONAL SECTION BEGINS ****************************/

// utility #1 - receive thread function
void *ReceiveData(void *arg)
{
    // needs thread function to receive data
    // remembeer to implement non-blocking receive_from call here

    char buffer[SIZE_BUF];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);



    while (is_running)
    {
        // needs to implement receive_from logic with non-blocking mode handled by socket flags

        memset(buffer, 0, SIZE_BUF);
        ssize_t msg_len = recvfrom( fd_socket, buffer, SIZE_BUF, 0, (struct sockaddr *)&sender_addr, &sender_len );

        if(msg_len > 0)
        {
            pthread_mutex_lock(&mutex_log);

            // let's attempt to parse the received message as a log level command now
            int new_level;
            if ( sscanf(buffer, "Set Log Level=%d", &new_level) == 1 )
            {
                if (new_level >= DEBUG && new_level <= CRITICAL)
                {
                    log_level_current = static_cast<LOG_LEVEL>(new_level);
                    std::cout << "Log level updated to " << new_level << std::endl;
                }
                else
                {
                    std::cout << "Received invalid log level. " << new_level << std::endl;
                }
            }

            pthread_mutex_unlock(&mutex_log);
        }
        else
        {
            sleep(1);
        }
    }

    return nullptr;
}


// utility #2 - Logger initializer -> called from the main()
int InitializeLog(void)
{
    // creates fd for UDP
    // needs to add sock_nonblock? 
    if ((fd_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("\nERROR: socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // socket -> non blocking
    int flag = fcntl(fd_socket, F_GETFL, 0);
    if(flag < 0)
    {
        perror("\nERROR: socket flag non blocking issue\n");
        exit(EXIT_FAILURE);
    }

    // if non blocking -> success
    flag |= O_NONBLOCK;
    if (fcntl(fd_socket, F_SETFL, flag) < 0)
    {
        perror("\nERROR: setting socket non blocking");
        exit(EXIT_FAILURE);
    }

    // upon success, then
    memset(&addr_server, 0, sizeof(addr_server));

    // configures the server info -> ipv4, port#, ip
    addr_server.sin_family = AF_INET;   
    addr_server.sin_port = htons(PORT_SERVER);
    if ( inet_pton(AF_INET, ADDRESS_SERVER, &addr_server.sin_addr) <=0 )       // prototyping purposes -> ANY ip
    {
        perror("inet_pton failed...\n");
        exit(EXIT_FAILURE);
    }

    // logger bind() itself to receive data
    memset( &addr_logger, 0, sizeof(addr_logger) );
    addr_logger.sin_family = AF_INET;
    addr_logger.sin_addr.s_addr = inet_addr(ADDRESS_LOGGER);
    addr_logger.sin_port = htons(PORT_LOGGER);

    if ( bind( fd_socket, (struct sockaddr *)&addr_logger, sizeof(addr_logger) ) <0 )
    {
        perror("bind() failed...\n");
        exit(EXIT_FAILURE);
    }

    // initializes mutex
    if(pthread_mutex_init(&mutex_log, NULL) != 0)
    {
        perror("\nERROR: mutex init has failed\n");
        exit(EXIT_FAILURE);
    }

    // starts receiving thread
    if (pthread_create(&thread_receive, NULL, &ReceiveData, NULL) != 0)
    {
        perror("\nError: failed to create the receive thread\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}


// utility #3 - will set the filter log level and store in a variable global within Logger.cpp.
void SetLogLevel(LOG_LEVEL level)
{
    pthread_mutex_lock(&mutex_log);
    log_level_current = level;
    pthread_mutex_unlock(&mutex_log);
}


// utility #4 - send the logs to the Server
void Log(LOG_LEVEL level, const char *prog, const char *func, int line, const char *message)
{
    // discard message if it is lower than the current threshold
    if (level < log_level_current)  return;

    // stages the message
    time_t now = time(nullptr);
    struct tm *tm_info;
    char dt[30];     
    tm_info = localtime(&now);
    strftime(dt, 30, "%Y-%m-%d %H:%M:%S", tm_info);     // controlled formatting

    char buffer_local[SIZE_BUF];
    memset(buffer_local, 0, SIZE_BUF);

    const char *level_str[] = {"DEBUG", "WARNING", "ERROR", "CRITICAL"};
    int len = snprintf(buffer_local, SIZE_BUF, "%s %s %s: %s: %d %s\n", dt, level_str[level], prog, func, line, message);
    
    if (len < 0 || len>= SIZE_BUF) {}       // handles snprintf error, etc.

    // mutexes the critical section
    pthread_mutex_lock(&mutex_log);
    {
        if (sendto(fd_socket, buffer_local, strlen(buffer_local), 0, (struct sockaddr *)&addr_server, sizeof(addr_server)) < 0)
        {
            perror("Error sending stuff to the server...\n");
            exit(EXIT_FAILURE);
        }
    }
    pthread_mutex_unlock(&mutex_log);
}


// utility #5 - will stop the receive thread via an is_running flag and close the file descriptor.
void ExitLog(void)
{
    is_running = false;

    if(recv_thread.joinable()) recv_thread.join();

    pthread_mutex_destroy(&mutex_log);

    close(fd_socket);
}

/********************************** FUNCTIONAL SECTION ENDS ********************************/