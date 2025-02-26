#include "leveldb/db.h"
#include <cassert>
#include <iostream>
#include <libpmemobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
    #include "ff_api.h"
}   
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <sys/ioctl.h>
#include <errno.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_malloc.h>
#include <sstream>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>  // 🔹 加入這行，讓 usleep() 可用
#include "leveldb/cache.h"
#include <memory>
#include "leveldb/filter_policy.h"




using namespace std;
using namespace leveldb;

#define LAYOUT_NAME "rweg"
#define MAX_BUF_SIZE 65536
#define RING_SIZE 1024

int kqueue_fd;
const std::string db_path = "/home/oslab/Desktop/john/leveldb/log";

struct rte_ring *request_ring;
struct rte_ring *response_ring;

struct event_args {
    int listen_fd;
    leveldb::DB* db;
};

struct client_request {
    int fd;  // 🔹 存放客戶端的 socket 描述符
    char buffer[MAX_BUF_SIZE];  // 🔹 存放請求數據
};

void init_fstack(int argc, char* argv[]) {
    const char* config_path = "/home/oslab/Desktop/john/leveldb/app/config.ini";
    const char* ff_argv[] = {argv[0], "--conf", config_path};
    int ff_argc = 3;
    if (ff_init(ff_argc, (char**)ff_argv) < 0) {
        fprintf(stderr, "Failed to initialize F-Stack.\n");
        exit(1);
    }
    printf("-------F-Stack initialized successfully.--------\n");
}

leveldb::DB* init_leveldb(const std::string& db_path) {
    leveldb::Options options;
    options.create_if_missing = true;

    options.paranoid_checks = false;
    options.compression = leveldb::kNoCompression;
    options.reuse_logs = true;
    options.write_buffer_size = 64 * 1024 * 1024;
    options.max_file_size = 128 * 1024 * 1024;
    options.block_size = 64 * 1024;
    options.block_cache = leveldb::NewLRUCache(128 * 1024 * 1024);

    // 🔹 修正 NewBloomFilterPolicy 未識別的問題
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);

    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, db_path, &db);
    if (!status.ok()) {
        fprintf(stderr, "❌ Failed to open LevelDB: %s\n", status.ToString().c_str());
        exit(1);
    }
    printf("------------LevelDB initialized successfully----------------\n");
    return db;
}


int create_server_socket(int port) {
    int listen_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int flag = 1;
    if (ff_ioctl(listen_fd, FIONBIO, &flag) < 0) {
        perror("Failed to set non-blocking mode");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");     
    server_addr.sin_port = htons(9000);

    if (ff_bind(listen_fd, (const struct linux_sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (ff_listen(listen_fd, 10) < 0) {
        perror("Listen failed");
        exit(1);
    }

    kqueue_fd = ff_kqueue();
    if (kqueue_fd < 0) {
        perror("ff_kqueue");
        return -1;
    }
    printf("kqueue_fd: %d, listen_fd: %d\n", kqueue_fd, listen_fd);

    struct kevent event;
    EV_SET(&event, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    int ret = ff_kevent(kqueue_fd, &event, 1, NULL, 1, NULL);
    if (ret < 0) {
        perror("ff_kevent: failed to register listen_fd");
        ff_close(listen_fd);
    } else {
        printf("ff_kevent: listen_fd successfully registered,listen_fd = %d, kqueue_fd = %d\n", listen_fd, kqueue_fd);
       //printf("listen_fd = %d\n" , listen_fd);

    }

    printf("Server socket created and listening on port %d.\n", port);
    return listen_fd;
}


void init_dpdk_ring() {
    request_ring = rte_ring_create("REQUEST_RING", RING_SIZE, SOCKET_ID_ANY, RING_F_SP_ENQ | RING_F_SC_DEQ);
    response_ring = rte_ring_create("RESPONSE_RING", RING_SIZE, SOCKET_ID_ANY, RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (!request_ring || !response_ring) {
        fprintf(stderr, "Failed to create rings in Hugepage\n");
        exit(1);
    }

}

void process_client(int fd, leveldb::DB* db) {
    struct client_request *req = (struct client_request*)rte_malloc(NULL, sizeof(struct client_request), 0);
    if (!req) return;
    req->fd = fd;
    int bytes_read = ff_read(fd, req->buffer, MAX_BUF_SIZE - 1);
    
    if (bytes_read > 0) {
        req->buffer[bytes_read] = '\0';
        printf("📥 Received request: %s (fd: %d)\n", req->buffer, fd);  // 🔹 Debug log

        if (rte_ring_enqueue(request_ring, req) < 0) {
            printf("⚠️ request_ring is full, dropping request from fd = %d\n", fd);
            rte_free(req);
        }
    } else {
        printf("⚠️ Client closed connection: fd = %d\n", fd);
        rte_free(req);
    }
}


void send_response() {
    struct client_request *resp;
    while (rte_ring_dequeue(response_ring, (void **)&resp) == 0) {  // 🔹 迴圈確保所有回應都發送
        if (!resp) continue;

        int client_fd = resp->fd;
        char *response = resp->buffer;

        printf("📤 Sending response to fd = %d: %s\n", client_fd, response);  // 🔹 Debug log

        struct ff_zc_mbuf zmbuf;
        if (ff_zc_mbuf_get(&zmbuf, MAX_BUF_SIZE) == 0) {
            memcpy(zmbuf.bsd_mbuf, response, strlen(response));
            ff_zc_mbuf_write(&zmbuf, response, strlen(response));
        } else {
            ff_write(client_fd, response, strlen(response));  // 🔹 發送回應
        }

        rte_free(resp);
    }
}




void *leveldb_worker(void *arg) {
    leveldb::DB* db = (leveldb::DB*)arg;
    while (1) {
        struct client_request *requests[32];
        unsigned num_dequeued = rte_ring_dequeue_bulk(request_ring, (void**)requests, 32, NULL);
        if (num_dequeued == 0) {
            continue;
        }

        for (unsigned i = 0; i < num_dequeued; i++) {
            struct client_request *req = requests[i];
            int client_fd = req->fd;
            std::string input(req->buffer);
            std::string command, key, value;
            std::istringstream iss(input);
            iss >> command;

            printf("🔄 Processing request: %s (fd: %d)\n", req->buffer, client_fd);  // 🔹 Debug log

            char *response = (char*)rte_malloc(NULL, MAX_BUF_SIZE, 0);
            if (!response) continue;

            if (command == "PUT") {
                if (!(iss >> key >> value)) {
                    snprintf(response, MAX_BUF_SIZE, "PUT FAILED\n");
                } else {
                    leveldb::Status s = db->Put(leveldb::WriteOptions(), key, value);
                    snprintf(response, MAX_BUF_SIZE, "PUT SUCCESS: %s\n", key.c_str());
                }
            } else if (command == "GET") {
                if (!(iss >> key)) {
                    snprintf(response, MAX_BUF_SIZE, "GET FAILED\n");
                } else {
                    std::string value;
                    leveldb::Status s = db->Get(leveldb::ReadOptions(), key, &value);
                    snprintf(response, MAX_BUF_SIZE, "GET SUCCESS: %s\n", value.c_str());
                }
            }else {
                snprintf(response, MAX_BUF_SIZE, "UNKNOWN COMMAND\n");
            }

            struct client_request *resp = (struct client_request*)rte_malloc(NULL, sizeof(struct client_request), 0);
            if (resp) {
                resp->fd = client_fd;
                strcpy(resp->buffer, response);
                if (rte_ring_enqueue(response_ring, resp) < 0) {
                    printf("⚠️ response_ring is full, dropping response for fd = %d\n", resp->fd);
                    rte_free(resp);
                } else {
                    printf("📤 Enqueued response: %s (fd: %d)\n", response, client_fd);  // 🔹 Debug log
                }
            }

            rte_free(response);
            rte_free(req);
        }
    }
    return NULL;
}



int event_loop(void *arg) {
    struct event_args *args = (struct event_args *)arg;
    struct kevent events[128];

    int nevents = ff_kevent(kqueue_fd, NULL, 0, events, 10, NULL);
    if (nevents < 0) {
        perror("ff_kevent failed");
        return -1;
    }

    for (int i = 0; i < nevents; i++) {
        if (events[i].filter == EVFILT_READ) {
            if (events[i].ident == args->listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = ff_accept(args->listen_fd, (struct linux_sockaddr *)&client_addr, &client_len);
                if (client_fd < 0) {
                    perror("ff_accept failed");
                    printf("errno = %d (%s)\n", errno, strerror(errno));
                } else {
                    printf("New connection accepted: fd = %d, from IP: %s, port: %d\n",
                           client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                    int flag = 1;
                    if (ff_ioctl(client_fd, FIONBIO, &flag) < 0) {
                        perror("Failed to set non-blocking mode for client_fd");
                        ff_close(client_fd);
                    } else {
                        struct kevent client_event;
                        EV_SET(&client_event, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                        ff_kevent(kqueue_fd, &client_event, 1, NULL, 0, NULL);
                    }
                }
            } else {
                process_client(events[i].ident, args->db);
            }
        } else {
            printf("Unexpected event filter: %d\n", events[i].filter);
            if (events[i].flags & EV_ERROR) {
                fprintf(stderr, "Event error: %s\n", strerror(events[i].data));
            }
        }
    }

    // 🔹 **確保 `send_response()` 被呼叫**
    send_response();

    return 0;
}





int main(int argc, char* argv[]) {
    init_fstack(argc, argv);
    leveldb::DB* db = init_leveldb(db_path);
    init_dpdk_ring();
    pthread_t worker_thread;
    pthread_create(&worker_thread, NULL, leveldb_worker, (void*)db);
    int listen_fd = create_server_socket(9000);
    struct event_args args = {listen_fd, db};
    ff_run(event_loop, &args);
    ff_close(listen_fd);
    delete db;
    return 0;
}
