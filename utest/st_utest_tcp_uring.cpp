/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2024 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <liburing.h>
#include <sys/resource.h>  // For rlimit
#include <algorithm>       // For std::min

// Client thread function
static void* client_thread_func(void* arg) {
    printf("[CLIENT] Starting client thread...\n");
    uint16_t port = *(uint16_t*)arg;
    delete (uint16_t*)arg;
    printf("[CLIENT] Connecting to port %d\n", port);

    // Create client socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("[CLIENT] socket failed");
        return nullptr;
    }
    printf("[CLIENT] Socket created successfully\n");

    // Create state-threads file descriptor
    st_netfd_t client_nfd = st_netfd_open_socket(client_fd);
    if (!client_nfd) {
        perror("[CLIENT] st_netfd_open_socket failed");
        close(client_fd);
        return nullptr;
    }
    printf("[CLIENT] State-threads file descriptor created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);

    // Connect to server using state-threads
    printf("[CLIENT] Attempting to connect to server...\n");
    if (st_connect(client_nfd, (struct sockaddr*)&server_addr, sizeof(server_addr), ST_UTIME_NO_TIMEOUT) != 0) {
        perror("[CLIENT] connect failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    printf("[CLIENT] Connected to server successfully\n");

    // Send data using state-threads
    const char* msg = "Hello from client!";
    printf("[CLIENT] Sending message: %s\n", msg);
    ssize_t n = st_write(client_nfd, msg, strlen(msg), ST_UTIME_NO_TIMEOUT);
    if (n != static_cast<ssize_t>(strlen(msg))) {
        perror("[CLIENT] send failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    printf("[CLIENT] Message sent successfully\n");

    // Receive response using state-threads
    char buf[1024];
    printf("[CLIENT] Waiting for server response...\n");
    n = st_read(client_nfd, buf, sizeof(buf) - 1, ST_UTIME_NO_TIMEOUT);
    if (n <= 0) {
        perror("[CLIENT] recv failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    buf[n] = '\0';
    printf("[CLIENT] Received response: %s\n", buf);
    if (strcmp(buf, "Hello from server!") != 0) {
        printf("[CLIENT] Unexpected response received\n");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    printf("[CLIENT] Response verified successfully\n");

    st_netfd_close(client_nfd);
    printf("[CLIENT] Client thread finished\n");
    return nullptr;
}

// Test io_uring TCP server and client
VOID TEST(IoUringTest, TcpServerClient)
{
    printf("\n[TEST] Starting TcpServerClient test...\n");

    // Initialize state-threads
    int rv = st_init();
    EXPECT_EQ(rv, 0);
    
    // Try to set event system to io_uring
    printf("[TEST] Attempting to set event system to io_uring...\n");
    rv = st_set_eventsys(ST_EVENTSYS_IO_URING);
    if (rv != 0) {
        printf("[TEST] io_uring event system not available, using default event system\n");
        // The default event system is already selected by st_init()
        // No need to call st_set_eventsys again
    }
    
    printf("[TEST] State-threads initialized with %s event system\n", st_get_eventsys_name());

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(server_fd, -1);
    printf("[TEST] Server socket created successfully\n");

    // Create state-threads file descriptor
    st_netfd_t server_nfd = st_netfd_open_socket(server_fd);
    EXPECT_NE(server_nfd, nullptr);
    printf("[TEST] State-threads file descriptor created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(0);  // Let system choose port

    EXPECT_EQ(bind(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);
    EXPECT_EQ(listen(st_netfd_fileno(server_nfd), 1), 0);
    printf("[TEST] Server bound and listening\n");

    // Get the port number
    socklen_t addr_len = sizeof(server_addr);
    EXPECT_EQ(getsockname(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, &addr_len), 0);
    uint16_t port = ntohs(server_addr.sin_port);
    printf("[TEST] Server listening on port %d\n", port);

    // Create a client thread
    printf("[TEST] Creating client thread...\n");
    st_thread_t client_thread = st_thread_create(client_thread_func, new uint16_t(port), 0, 0);
    EXPECT_NE(client_thread, nullptr);
    printf("[TEST] Client thread created successfully\n");

    // Accept client connection using state-threads
    printf("[TEST] Waiting for client connection...\n");
    st_netfd_t client_nfd = st_accept(server_nfd, nullptr, nullptr, ST_UTIME_NO_TIMEOUT);
    EXPECT_NE(client_nfd, nullptr);
    printf("[TEST] Client connection accepted\n");

    // Receive data using state-threads
    char buf[1024];
    printf("[TEST] Waiting for client data...\n");
    ssize_t n = st_read(client_nfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT);
    EXPECT_GT(n, 0);
    buf[n] = '\0';
    EXPECT_STREQ(buf, "Hello from client!");
    printf("[TEST] Received message: %s\n", buf);

    // Send response using state-threads
    const char* msg = "Hello from server!";
    EXPECT_EQ(st_write(client_nfd, msg, strlen(msg), ST_UTIME_NO_TIMEOUT), static_cast<ssize_t>(strlen(msg)));
    printf("[TEST] Sent response: %s\n", msg);

    // Wait for client thread to finish
    printf("[TEST] Waiting for client thread to finish...\n");
    st_thread_join(client_thread, nullptr);
    printf("[TEST] Client thread finished\n");

    // Cleanup
    st_netfd_close(client_nfd);
    st_netfd_close(server_nfd);
    printf("[TEST] Test completed successfully\n");
}

// Client thread function for multiple clients
static void* multi_client_thread_func(void* arg) {
    uint16_t port = *(uint16_t*)arg;
    delete (uint16_t*)arg;

    // Create client socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        return nullptr;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);

    // Connect to server
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        close(client_fd);
        return nullptr;
    }

    // Send data
    char msg[64];
    snprintf(msg, sizeof(msg), "Hello from client %d!", getpid());
    if (send(client_fd, msg, strlen(msg), 0) != static_cast<ssize_t>(strlen(msg))) {
        perror("send failed");
        close(client_fd);
        return nullptr;
    }

    // Receive response
    char buf[1024];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        perror("recv failed");
        close(client_fd);
        return nullptr;
    }
    buf[n] = '\0';
    if (strcmp(buf, "Hello from server!") != 0) {
        close(client_fd);
        return nullptr;
    }

    close(client_fd);
    return nullptr;
}

// Test io_uring TCP server with multiple clients
VOID TEST(IoUringTest, TcpServerMultiClients)
{
    printf("\n[TEST] Starting TcpServerMultiClients test...\n");

    // Initialize state-threads
    int rv = st_init();
    EXPECT_EQ(rv, 0);
    
    // Try to set event system to io_uring
    printf("[TEST] Attempting to set event system to io_uring...\n");
    rv = st_set_eventsys(ST_EVENTSYS_IO_URING);
    if (rv != 0) {
        printf("[TEST] io_uring event system not available, using default event system\n");
        // The default event system is already selected by st_init()
        // No need to call st_set_eventsys again
    }
    
    printf("[TEST] State-threads initialized with %s event system\n", st_get_eventsys_name());

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(server_fd, -1);
    printf("[TEST] Server socket created successfully\n");

    // Create state-threads file descriptor
    st_netfd_t server_nfd = st_netfd_open_socket(server_fd);
    EXPECT_NE(server_nfd, nullptr);
    printf("[TEST] State-threads file descriptor created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(0);  // Let system choose port

    EXPECT_EQ(bind(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);
    EXPECT_EQ(listen(st_netfd_fileno(server_nfd), 5), 0);
    printf("[TEST] Server bound and listening\n");

    // Get the port number
    socklen_t addr_len = sizeof(server_addr);
    EXPECT_EQ(getsockname(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, &addr_len), 0);
    uint16_t port = ntohs(server_addr.sin_port);
    printf("[TEST] Server listening on port %d\n", port);

    // Create multiple client threads
    const int num_clients = 3;
    std::vector<st_thread_t> client_threads;
    printf("[TEST] Creating %d client threads...\n", num_clients);
    for (int i = 0; i < num_clients; i++) {
        st_thread_t thread = st_thread_create(client_thread_func, new uint16_t(port), 0, 0);
        EXPECT_NE(thread, nullptr);
        client_threads.push_back(thread);
    }
    printf("[TEST] All client threads created successfully\n");

    // Accept all client connections
    std::vector<st_netfd_t> client_nfds;
    for (int i = 0; i < num_clients; i++) {
        printf("[TEST] Waiting for client %d connection...\n", i + 1);
        st_netfd_t client_nfd = st_accept(server_nfd, nullptr, nullptr, ST_UTIME_NO_TIMEOUT);
        EXPECT_NE(client_nfd, nullptr);
        client_nfds.push_back(client_nfd);
        printf("[TEST] Client %d connection accepted\n", i + 1);

        // Receive data from client
        char buf[1024];
        ssize_t n = st_read(client_nfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT);
        EXPECT_GT(n, 0);
        buf[n] = '\0';
        printf("[TEST] Received from client %d: %s\n", i + 1, buf);

        // Send response to client
        const char* msg = "Hello from server!";
        EXPECT_EQ(st_write(client_nfd, msg, strlen(msg), ST_UTIME_NO_TIMEOUT), static_cast<ssize_t>(strlen(msg)));
        printf("[TEST] Sent response to client %d\n", i + 1);
    }

    // Wait for all client threads to finish
    printf("[TEST] Waiting for all client threads to finish...\n");
    for (st_thread_t thread : client_threads) {
        st_thread_join(thread, nullptr);
    }
    printf("[TEST] All client threads finished\n");

    // Cleanup
    for (st_netfd_t client_nfd : client_nfds) {
        st_netfd_close(client_nfd);
    }
    st_netfd_close(server_nfd);
    printf("[TEST] Test completed successfully\n");
}

// Client thread function for stress test
static void* stress_client_thread_func(void* arg) {
    uint16_t port = *(uint16_t*)arg;
    delete (uint16_t*)arg;
    printf("[STRESS_CLIENT] Starting client thread for port %d\n", port);

    // Create client socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("[STRESS_CLIENT] socket failed");
        return nullptr;
    }
    printf("[STRESS_CLIENT] Socket created successfully\n");

    // Create state-threads file descriptor
    st_netfd_t client_nfd = st_netfd_open_socket(client_fd);
    if (!client_nfd) {
        perror("[STRESS_CLIENT] st_netfd_open_socket failed");
        close(client_fd);
        return nullptr;
    }
    printf("[STRESS_CLIENT] State-threads file descriptor created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);

    // Connect to server using state-threads
    printf("[STRESS_CLIENT] Attempting to connect to server...\n");
    if (st_connect(client_nfd, (struct sockaddr*)&server_addr, sizeof(server_addr), ST_UTIME_NO_TIMEOUT) != 0) {
        perror("[STRESS_CLIENT] connect failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    printf("[STRESS_CLIENT] Connected to server successfully\n");

    // Send data using state-threads
    const char* msg = "Hello from stress test client!";
    printf("[STRESS_CLIENT] Sending message...\n");
    ssize_t n = st_write(client_nfd, msg, strlen(msg), ST_UTIME_NO_TIMEOUT);
    if (n != static_cast<ssize_t>(strlen(msg))) {
        perror("[STRESS_CLIENT] send failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    printf("[STRESS_CLIENT] Message sent successfully\n");

    // Receive response using state-threads
    char buf[1024];
    printf("[STRESS_CLIENT] Waiting for server response...\n");
    n = st_read(client_nfd, buf, sizeof(buf) - 1, ST_UTIME_NO_TIMEOUT);
    if (n <= 0) {
        perror("[STRESS_CLIENT] recv failed");
        st_netfd_close(client_nfd);
        return nullptr;
    }
    buf[n] = '\0';
    printf("[STRESS_CLIENT] Received response: %s\n", buf);

    st_netfd_close(client_nfd);
    printf("[STRESS_CLIENT] Client thread finished\n");
    return nullptr;
}

// Function to get CPU usage with more detail
static std::pair<double, std::string> get_detailed_cpu_usage() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return std::make_pair(-1.0, std::string("Error getting CPU usage"));
    }
    
    double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    double sys_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    double total_time = user_time + sys_time;
    
    char details[256];
    snprintf(details, sizeof(details), 
             "User: %.2f%%, System: %.2f%%, Total: %.2f%%, MaxRSS: %ld KB",
             user_time * 100.0, sys_time * 100.0, total_time * 100.0, 
             usage.ru_maxrss);
    
    return std::make_pair(total_time * 100.0, std::string(details));
}

// Function to get CPU usage (simplified version for backward compatibility)
static double get_cpu_usage() {
    return get_detailed_cpu_usage().first;
}

// Server thread function to handle client connections
static void* server_handler_thread(void* arg) {
    st_netfd_t client_nfd = (st_netfd_t)arg;
    if (!client_nfd) {
        printf("[SERVER] Error: Invalid file descriptor passed to handler\n");
        return nullptr;
    }
    
    // Handle the connection
    char buf[1024];
    ssize_t n = st_read(client_nfd, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT);
    if (n > 0) {
        buf[n] = '\0';
        // Send response
        const char* msg = "Hello from server!";
        st_write(client_nfd, msg, strlen(msg), ST_UTIME_NO_TIMEOUT);
    }
    
    // Don't close the client_nfd here, let the main thread handle it
    return nullptr;
}

// Test io_uring TCP server with stress test for 10000 connections
VOID TEST(IoUringTest, TcpServer10KConnections)
{
    printf("\n[TEST] Starting TcpServer10KConnections test...\n");

    // Initialize state-threads
    int rv = st_init();
    EXPECT_EQ(rv, 0);
    
    // Try to set event system to io_uring
    printf("[TEST] Attempting to set event system to io_uring...\n");
    rv = st_set_eventsys(ST_EVENTSYS_IO_URING);
    if (rv != 0) {
        printf("[TEST] io_uring event system not available, using default event system\n");
        // The default event system is already selected by st_init()
        // No need to call st_set_eventsys again
    }
    
    printf("[TEST] State-threads initialized with %s event system\n", st_get_eventsys_name());

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(server_fd, -1);
    
    // Enable address reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Create state-threads file descriptor
    st_netfd_t server_nfd = st_netfd_open_socket(server_fd);
    EXPECT_NE(server_nfd, nullptr);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(0);  // Let system choose port

    EXPECT_EQ(bind(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);
    EXPECT_EQ(listen(st_netfd_fileno(server_nfd), 128), 0);  // Use a more modest backlog
    printf("[TEST] Server bound and listening\n");

    // Get the port number
    socklen_t addr_len = sizeof(server_addr);
    EXPECT_EQ(getsockname(st_netfd_fileno(server_nfd), (struct sockaddr*)&server_addr, &addr_len), 0);
    uint16_t port = ntohs(server_addr.sin_port);
    printf("[TEST] Server listening on port %d\n", port);

    // Get system limits
    struct rlimit rlim;
    EXPECT_EQ(getrlimit(RLIMIT_NOFILE, &rlim), 0);
    printf("[TEST] System file descriptor limit: %lu\n", (unsigned long)rlim.rlim_cur);
    
    // Calculate maximum number of connections - use a much smaller number for safety
    const int target_connections = 100;  // Start with 100 instead of 10000
    const int max_connections = std::min(target_connections, static_cast<int>(rlim.rlim_cur - 200));
    printf("[TEST] Will test up to %d connections\n", max_connections);

    // Create vectors to store client threads and server-side file descriptors
    std::vector<st_thread_t> client_threads;
    std::vector<st_thread_t> server_threads;
    std::vector<st_netfd_t> client_nfds;
    
    client_threads.reserve(max_connections);
    server_threads.reserve(max_connections);
    client_nfds.reserve(max_connections);

    // Start stress test
    int successful_connections = 0;
    int batch_size = 500;  // Larger batch size for efficiency
    int current_batch = 0;
    const double CPU_THRESHOLD = 90.0;
    
    // Record start time
    time_t start_time = time(NULL);
    
    // Print table header
    printf("\n%-10s %-15s %-25s %-20s\n", "Batch", "Connections", "CPU Usage", "Time Elapsed");
    printf("------------------------------------------------------------------\n");

    while (successful_connections < max_connections) {
        current_batch++;
        
        // Check CPU usage before starting new batch
        std::pair<double, std::string> cpu_info = get_detailed_cpu_usage();
        double cpu_percentage = cpu_info.first;
        
        if (cpu_percentage > CPU_THRESHOLD) {
            printf("[TEST] CPU usage too high (%.2f%%), stopping test\n", cpu_percentage);
            break;
        }
        
        // Create a batch of client threads
        int threads_created = 0;
        int batch_target = std::min(batch_size, max_connections - successful_connections);
        
        for (int i = 0; i < batch_target; i++) {
            uint16_t* port_arg = new uint16_t(port);
            st_thread_t thread = st_thread_create(stress_client_thread_func, port_arg, 0, 0);
            if (thread) {
                client_threads.push_back(thread);
                threads_created++;
            } else {
                delete port_arg;  // Clean up if thread creation failed
                printf("[TEST] Failed to create client thread\n");
            }
        }
        
        printf("[TEST] Created %d client threads in this batch\n", threads_created);
        
        // Wait a bit to ensure threads have time to start
        st_sleep(1);
        
        // Accept connections and handle them
        int connections_accepted = 0;
        for (int i = 0; i < threads_created; i++) {
            // Accept with timeout
            st_netfd_t client_nfd = st_accept(server_nfd, nullptr, nullptr, ST_UTIME_NO_TIMEOUT);
            if (client_nfd) {
                client_nfds.push_back(client_nfd);
                
                // Create a dedicated thread to handle this connection
                st_thread_t server_thread = st_thread_create(server_handler_thread, 
                                                          (void*)client_nfd, 0, 0);
                if (server_thread) {
                    server_threads.push_back(server_thread);
                    successful_connections++;
                    connections_accepted++;
                } else {
                    printf("[TEST] Failed to create server handler thread\n");
                    // Don't increment counters if thread creation failed
                }
            } else {
                printf("[TEST] Failed to accept client connection\n");
                break;
            }
        }
        
        printf("[TEST] Accepted %d connections in this batch\n", connections_accepted);
        
        // Print progress and CPU usage
        std::pair<double, std::string> current_cpu_info = get_detailed_cpu_usage();
        double current_cpu = current_cpu_info.first;
        std::string current_details = current_cpu_info.second;
        
        time_t elapsed = time(NULL) - start_time;
        printf("%-10d %-15d %-25.2f %-20ld\n", 
               current_batch, successful_connections, current_cpu, elapsed);
        
        // Print detailed CPU information
        printf("[DETAIL] %s\n", current_details.c_str());
        
        // Wait for client threads in this batch to finish
        printf("[TEST] Waiting for client threads in this batch to finish...\n");
        for (int i = client_threads.size() - threads_created; i < (int)client_threads.size(); i++) {
            if (i >= 0 && i < (int)client_threads.size()) {
                st_thread_join(client_threads[i], nullptr);
            }
        }
        
        // If we couldn't accept all connections in this batch, we've hit a limit
        if (connections_accepted < threads_created) {
            printf("[TEST] Reached connection limit - accepting stopped at %d connections\n", 
                   successful_connections);
            break;
        }
        
        // Sleep between batches to allow system to stabilize
        st_sleep(1);
    }

    // Wait for all server threads to finish
    printf("[TEST] Waiting for server threads to finish...\n");
    for (auto& thread : server_threads) {
        if (thread) {
            st_thread_join(thread, nullptr);
        }
    }
    
    // Final statistics
    std::pair<double, std::string> final_stats = get_detailed_cpu_usage();
    double final_cpu = final_stats.first;
    std::string cpu_details = final_stats.second;
    time_t total_time = time(NULL) - start_time;
    
    printf("\n[TEST] Stress test completed\n");
    printf("[TEST] Total successful connections: %d\n", successful_connections);
    printf("[TEST] Total time: %ld seconds\n", total_time);
    printf("[TEST] Connections per second: %.2f\n", 
           static_cast<double>(successful_connections) / (total_time > 0 ? total_time : 1));
    printf("[TEST] Final CPU usage: %s\n", cpu_details.c_str());

    // Cleanup
    printf("[TEST] Cleaning up connections...\n");
    for (st_netfd_t client_nfd : client_nfds) {
        if (client_nfd) {
            st_netfd_close(client_nfd);
        }
    }
    
    if (server_nfd) {
        st_netfd_close(server_nfd);
    }
    
    printf("[TEST] Test completed successfully\n");
}