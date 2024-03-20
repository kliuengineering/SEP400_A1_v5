/********************************** SETUP LIBRARIES BEGINS ********************************/

#include <fstream>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

/********************************** SETUP LIBRARIES ENDS **********************************/





/********************************** SETUP SECTION BEGINS **********************************/

// macroes
#define PORT_SERVER 8080
#define PORT_LOGGER 9090
#define SIZE_BUF 1024

const char ADDRESS_SERVER[] = "127.0.0.1";
const char ADDRESS_CLIENT[] = "127.0.0.1";

// global vars
std::atomic<bool> is_running(true);
int fd_socket;
std::mutex mutex_log;
std::thread recv_thread;
struct sockaddr_in logger_addr;

/********************************** SETUP SECTION ENDS ***********************************/





/********************************** FUNCTIONAL SECTION BEGINS ****************************/

// utility #1 - clear input stream
void ClearInputBuffer()
{
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');     // just ignore the rest
}


// utility #2 - signal handler -> graceful shutdown
void ShutDownHandler(int sig)
{
    if(sig == SIGINT) is_running = false;
}


// utility #3 - receive thread
void ReceiveMessage()
{
    char buffer[SIZE_BUF];
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    // accesses the log file
    int log_fd = open("ServerLog", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (log_fd < 0)
    {
        perror("Failed to open the log file");
        return;
    }

    while(is_running)
    {
        std::memset(buffer, 0, SIZE_BUF);
        ssize_t msg_len = recvfrom(fd_socket, buffer, SIZE_BUF, 0, (struct sockaddr *)&client_addr, &len);

        if(msg_len > 0)
        {
            // mutex lock
            std::lock_guard<std::mutex> guard(mutex_log);

            // write the received message to the log
            write(log_fd, buffer, msg_len);

            // add a new line
            // write(log_fd, "\n", 1);
        }
        else
        {
            sleep(1);
        }
    
    }

    close(log_fd);
}


// utility #4 - sets the log level 
void SetLogLevel()
{
    int level;
    char buf[SIZE_BUF];

    std::cout << "Enter log level (0=DEBUG, 1=WARNING, 2=ERROR, 3=CRITICAL) -> ";
    std::cin >> level;
    ClearInputBuffer();

    // input validation
    if(level < 0 || level >3)
    {
        std::cout << "Invalid entry, please enter only 0, 1, 2, 3...\n";
        return;
    }

    // stage the command being sent out
    memset(buf, 0, SIZE_BUF);
    int len = snprintf(buf, SIZE_BUF, "Set Log Level=%d", level) + 1;

    // mutex locking
    {
        std::lock_guard<std::mutex> guard(mutex_log);

        // sends command to the Logger
        if ( sendto( fd_socket, buf, len, 0, (struct sockaddr *)&logger_addr, sizeof(logger_addr) )  < 0 )
        {
            perror("Failed to send log level command...\n");
        }
    }
}


// utility #5 - dumps log onto the terminal 
void DumpLogFile()
{
    mutex_log.lock();

    std::ifstream log_file("ServerLog", std::ifstream::in);
    if (!log_file.is_open())
    {
        std::cerr << "Failed to open the log file for reading...\n";
        mutex_log.unlock();
        return;
    }

    std::string line;
    while (getline(log_file, line))
    {
        std::cout << line << std::endl;
    }

    log_file.close();
    mutex_log.unlock();

    std::cout << "Press any key to continue...\n";
    std::cin.get();
    ClearInputBuffer();
    system("clear");
}


// utility #6 - separate thread for using input 
void HandleUserInput() 
{
    int option;
    while (is_running) 
    {
        std::cout << "1. Set log level\n";
        std::cout << "2. Dump log file\n";
        std::cout << "0. Shut down gracefully\n";
        std::cout << "Your option is... -> ";
        std::cin >> option;
        
        if (std::cin.fail()) 
        {
            ClearInputBuffer();
            continue; // Skip the rest of the loop
        }

        switch (option) 
        {
            case 1: // Handle setting log level
            {
                SetLogLevel();
                puts("");
                break;
            }

            case 2: // Handle dumping the log file
            {
                DumpLogFile();
                puts("");
                break;
            }

            case 0: // Handle graceful shutdown
            {
                is_running = false;
                std::cout << "The server is now shutting down...";
                puts("");
                break;
            }

            default: // Invalid input handling
            {
                std::cout << "Invalid option. Please try again.";
                puts("");
                puts("");
                break;
            }
        }
    }
}

/********************************** FUNCTIONAL SECTION ENDS ******************************/





// main()
int main(void)
{
    signal(SIGINT, ShutDownHandler);

    // non-blocking UDP socket
    fd_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd_socket < 0)
    {
        perror("Socket creation -> failed\n");
        exit(EXIT_FAILURE);
    }

    // non blocking sock
    int flag = fcntl(fd_socket, F_GETFL, 0);
    fcntl(fd_socket, F_SETFL, flag | O_NONBLOCK);

    // bind()
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    
    if( inet_pton(AF_INET, ADDRESS_SERVER, &server_addr.sin_addr) <=0 )
    {
        perror("inet_pton failed...\n");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_port = htons(PORT_SERVER);

    if ( bind(fd_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 )
    {
        perror("Bind -> failed.\n");
        close(fd_socket);
        exit(EXIT_FAILURE);
    }

    // logger initialization
    memset(&logger_addr, 0, sizeof(logger_addr));
    logger_addr.sin_family = AF_INET;
    logger_addr.sin_port = htons(PORT_LOGGER); // change to 9090?
    inet_pton(AF_INET, ADDRESS_SERVER, &logger_addr.sin_addr);

    // Starting the receive thread
    recv_thread = std::thread(ReceiveMessage);

    // start a thread for user input
    std::thread input_thread(HandleUserInput);

    // join() input thread
    if (input_thread.joinable())
    {
        input_thread.join();
    }

    // join() receive thread
    if (recv_thread.joinable())
    {
        recv_thread.join();
    }

    // close fd
    close(fd_socket);
    return 0;
}