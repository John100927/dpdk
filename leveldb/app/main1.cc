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
#include <unistd.h>  // for usleep()
#include <sys/ioctl.h>
#include <errno.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <sstream>
#include <algorithm>
#include "leveldb/filter_policy.h"


using namespace std;
using namespace leveldb;

#define LAYOUT_NAME "rweg"
#define MAX_BUF_LEN 31
#define MAX_BUF_SIZE 65536

int kqueue_fd;

const std::string db_path = "/home/oslab/Desktop/john/leveldb/log";


struct event_args {
    int listen_fd;
    leveldb::DB* db;
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

    // 🔹 優化 LevelDB 效能
    options.paranoid_checks = false;  // 避免額外的數據完整性檢查，提高讀寫速度
    options.compression = leveldb::kNoCompression;  // 關閉壓縮，提高寫入速度
    options.reuse_logs = true;  // 允許 LevelDB 重新使用舊的 WAL (Write-Ahead Logging) 檔案
    options.write_buffer_size = 64 * 1024 * 1024;  // 增加緩衝區大小 (64MB)
    options.max_file_size = 128 * 1024 * 1024;  // 降低 LevelDB 產生過多小文件的機率 (128MB)
    options.block_size = 64 * 1024;  // 設定 block size (64KB)
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);  // 啟用 Bloom Filter，加速 `GET` 查詢

    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, db_path, &db);
    if (!status.ok()) {
        fprintf(stderr, "❌ Failed to open LevelDB: %s\n", status.ToString().c_str());
        exit(1);
    }
    printf("✅ LevelDB initialized successfully at %s.\n", db_path.c_str());
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


void process_client(int fd, leveldb::DB* db) {
    size_t buffer_size =1024 * 1024;  // 🔹 增加 buffer 大小，確保足夠處理 fill100k
    char* buffer = (char*)malloc(buffer_size);
    char* response = (char*)malloc(buffer_size);

    if (!buffer || !response) {
        printf("❌ 記憶體分配失敗\n");
        if (buffer) free(buffer);
        if (response) free(response);
        return;
    }

    int bytes_read = ff_read(fd, buffer, buffer_size - 1);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';  // 確保字串結尾

        // **移除換行符，確保 key 乾淨**
        std::string input(buffer);
        input.erase(std::remove(input.begin(), input.end(), '\n'), input.end());
        input.erase(std::remove(input.begin(), input.end(), '\r'), input.end());

        std::string command, key, value;
        std::istringstream iss(input);
        iss >> command;

        if (command == "PUT") {
            // **解析 `key` 和 `value`**
            if (!(iss >> key >> value)) {
                snprintf(response, buffer_size, "❌ PUT FAILED: Invalid format\n");
            } else {
                leveldb::WriteOptions write_options;
                //  write_options.sync = true; // **確保同步寫入**
                leveldb::Status s = db->Put(write_options, key, value);

                if (s.ok()) {
                    snprintf(response, buffer_size, "✅ PUT SUCCESS: %s\n", key.c_str());

                    // **確認數據是否真的寫入**
                    std::string verify_value;
                    leveldb::Status check_status = db->Get(leveldb::ReadOptions(), key, &verify_value);
                    if (check_status.ok()) {
                        // printf("📝 PUT 確認成功: key=%s, value=%s\n", key.c_str(), verify_value.c_str());
                    } else {
                        printf("❌ PUT 確認失敗: Key %s 不存在\n", key.c_str());
                    }

                } else {
                    snprintf(response, buffer_size, "❌ PUT FAILED: %s\n", s.ToString().c_str());
                }
            }
        } else if (command == "GET") {
            // **確保 `key` 沒有換行符**
            if (!(iss >> key)) {
                snprintf(response, buffer_size, "❌ GET FAILED: Invalid format\n");
            } else {
                std::string value;
                leveldb::ReadOptions read_options;
                // read_options.fill_cache = true;         // **確保從 cache 讀取**
                // read_options.verify_checksums = true;   // **檢查數據完整性**

                leveldb::Status s = db->Get(read_options, key, &value);

                if (s.ok()) {
                    snprintf(response, buffer_size, "✅ GET SUCCESS: %s\n", value.c_str());
                } else {
                    snprintf(response, buffer_size, "❌ GET FAILED: Key not found\n");
                }
            }
        } else if (command == "EXIT") {
            // **處理 EXIT 操作**
            snprintf(response, buffer_size, "👋 Goodbye!\n");
            ff_write(fd, response, strlen(response));
            printf("🔴 Client requested to close connection.\n");

            // **釋放記憶體**
            free(buffer);
            free(response);

            ff_close(fd);
            return;  // **結束該連接處理**
        } else {
            snprintf(response, buffer_size, "❓ Unknown command\n");
        }

        // **發送回應**
        ff_write(fd, response, strlen(response));
    } else {
        printf("⚠️ Client closed the connection.\n");
        // **釋放記憶體**
    free(buffer);
    free(response);

    ff_close(fd);  // 🔹 保持原本邏輯，這行不變
    }

}




int event_loop(void *arg) {
    struct event_args *args = (struct event_args *)arg;
    struct kevent events[64];
    // printf("Waiting for events on kqueue_fd: %d...\n", kqueue_fd);
    int nevents = ff_kevent(kqueue_fd, NULL, 0, events, 10, NULL);
    // printf("Number of events detected: %d\n", nevents);
        if (nevents < 0) {
            perror("ff_kevent failed");
            return -1;
        }        
        for (int i = 0; i < nevents; i++) {
            // printf("Event triggered: ident = %lu, filter = %d\n", events[i].ident, events[i].filter);

            if (events[i].filter == EVFILT_READ) {
                //  printf("Readable event triggered on ident = %lu\n", events[i].ident);
                //  printf("listen_fd = %d\n", args->listen_fd);

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
                        
                        // 設置新客戶端為非阻塞模式
                        int flag = 1;
                        if (ff_ioctl(client_fd, FIONBIO, &flag) < 0) {
                            perror("Failed to set non-blocking mode for client_fd");
                            ff_close(client_fd);
                        } else {
                            // 將新客戶端 fd 添加到 kqueue
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
    return 0;
}


int main(int argc, char* argv[]) {
    init_fstack(argc, argv);
    leveldb::DB* db = init_leveldb(db_path);
    int listen_fd = create_server_socket(9000);
    struct event_args args = {listen_fd, db};
    ff_run(event_loop, &args);
    ff_close(listen_fd);
    delete db;
    return 0;
}