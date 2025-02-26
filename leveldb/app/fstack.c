#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff_api.h>
#include <arpa/inet.h>
#include <leveldb/c.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>


#define PORT 8080
#define BUFFER_SIZE 1024
int listen_fd;
int kqueue_fd;

struct event_args {
    int listen_fd;
    leveldb_t* db;
    leveldb_readoptions_t* read_options;
    leveldb_writeoptions_t* write_options;
};

// 初始化 LevelDB
// 初始化 LevelDB
const char* db_path = "/home/oslab/Desktop/john/leveldb2/leveldb/testdb"; // 資料庫目錄設置為當前目錄下的 "testdb"
leveldb_t* init_leveldb(const char* db_path, leveldb_options_t* options, char** err) {
    leveldb_t* db = leveldb_open(options, db_path, err);
    if (*err != NULL) {
        fprintf(stderr, "Failed to open LevelDB: %s\n", *err);
        exit(1);
    }
    return db;
}

// 處理 GET 請求
void handle_get(leveldb_t* db, const char* key, char* response, size_t response_size, leveldb_readoptions_t* read_options) {
    size_t read_len;
    char* value = leveldb_get(db, read_options, key, strlen(key), &read_len, NULL);
    if (value) {
        snprintf(response, response_size, "Value: %s\n", value);
        printf("GET successful: key = %s, value = %s\n", key, value);
        free(value);
    } else {
        snprintf(response, response_size, "Key not found\n");
        printf("GET failed: key = %s not found\n", key);
    }
}

// 處理 PUT 請求
void handle_put(leveldb_t* db, const char* key, const char* value, char* response, size_t response_size, leveldb_writeoptions_t* write_options) {
    leveldb_put(db, write_options, key, strlen(key), value, strlen(value), NULL);
    snprintf(response, response_size, "PUT SUCCESS\n");
    printf("PUT successful: key = %s, value = %s\n", key, value);
}

// 處理客戶端請求
void process_client(int fd, leveldb_t* db, leveldb_readoptions_t* read_options, leveldb_writeoptions_t* write_options) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};
    int bytes_read = ff_read(fd, buffer, sizeof(buffer));

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0'; // 確保字符串以 NULL 結尾
        printf("Received: %s\n", buffer);

        // 處理指令
        if (strncmp(buffer, "GET", 3) == 0) {
            char key[BUFFER_SIZE];
            sscanf(buffer + 4, "%s", key); // 提取 key
            handle_get(db, key, response, sizeof(response), read_options);
        } else if (strncmp(buffer, "PUT", 3) == 0) {
            char key[BUFFER_SIZE], value[BUFFER_SIZE];
            sscanf(buffer + 4, "%s %s", key, value); // 提取 key 和 value
            handle_put(db, key, value, response, sizeof(response), write_options);
        } else if (strncmp(buffer, "DEL", 3) == 0) {
            char key[BUFFER_SIZE];
            sscanf(buffer + 4, "%s", key); // 提取 key
            leveldb_delete(db, write_options, key, strlen(key), NULL);
            snprintf(response, sizeof(response), "DELETE SUCCESS\n");
        } else if (strncmp(buffer, "EXIT", 4) == 0) {
            snprintf(response, sizeof(response), "Goodbye!\n");
            ff_write(fd, response, strlen(response));
            ff_close(fd);
            return;
        } else {
            snprintf(response, sizeof(response), "Unknown command\n");
        }

        // 回應客戶端
        ff_write(fd, response, strlen(response));
    } else if (bytes_read == 0) {
        printf("Client closed the connection.\n");
        ff_close(fd);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("No data available for now, try again later. errno = %d\n", errno);
    } else {
        perror("ff_read failed");
        ff_close(fd);
    }
}




// 事件循環邏輯
int ff_event_loop(void *arg) {
    struct event_args *args = (struct event_args *)arg;
    struct kevent events[10]; // 儲存事件

    // 確認 listen_fd 是否仍在 kqueue
    // printf("🔍 Checking if listen_fd is still in kqueue: %d\n", args->listen_fd);
    struct kevent check_event;
    EV_SET(&check_event, args->listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    int check_ret = ff_kevent(kqueue_fd, &check_event, 1, NULL, 0, NULL);
    if (check_ret < 0) {
        perror("❌ listen_fd is NOT in kqueue");
    } else {
        // printf("✅ listen_fd is still in kqueue: %d\n", args->listen_fd);
    }

    // 等待事件發生，不設置超時機制
    int nevents = ff_kevent(kqueue_fd, NULL, 0, events, 10, NULL);

    if (nevents < 0) {
        perror("ff_kevent");
        return -1;
    }
    // printf("Number of events detected: %d\n", nevents);

    for (int i = 0; i < nevents; i++) {
        printf("Event triggered: ident = %lu, filter = %d\n", events[i].ident, events[i].filter);

        if (events[i].filter == EVFILT_READ) { // 可讀事件
            printf("Readable event triggered on ident = %lu\n", events[i].ident);
            printf("listen_fd = %d\n", args->listen_fd);

            if (events[i].ident == args->listen_fd) { // 新連接到來
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int client_fd = ff_accept(args->listen_fd, (struct linux_sockaddr *)&client_addr, &client_len);
                if (client_fd < 0) {
                    perror("ff_accept failed");
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
            } else { // 客戶端 fd 有數據可讀
                process_client(events[i].ident, args->db, args->read_options, args->write_options);
            }
        } else { // 非預期事件或錯誤
            printf("Unexpected event filter: %d\n", events[i].filter);
            if (events[i].flags & EV_ERROR) {
                fprintf(stderr, "Event error: %s\n", strerror(events[i].data));
            }
        }
    }

    return 0;
}


    
int main(int argc, char* argv[]) {
    const char* config_path = "/home/oslab/Desktop/john/leveldb/app/config.ini";

    // 初始化 F-Stack
    char* ff_argv[] = {argv[0], "--conf", (char*)config_path};
    int ff_argc = 3;
    if (ff_init(ff_argc, ff_argv) < 0) {
        fprintf(stderr, "Failed to initialize F-Stack.\n");
        return -1;
    }

    int listen_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket failed");
        return -1;
    }
    int flag = 1;
    if (ff_ioctl(listen_fd, FIONBIO, &  flag) < 0) {
        perror("Failed to set non-blocking mode");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // 清空結構體記憶體
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0"); // 綁定所有網路介面
    server_addr.sin_port = htons(8080);

    if (ff_bind(listen_fd, (const struct linux_sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return -1;
    }

    if (ff_listen(listen_fd, 10) < 0) {
        perror("Listen failed");
        return -1;
    }else{
        printf("ff_listen succeeded, waiting for connections...\n");
    }
    printf("Listening FD: %d\n", listen_fd);


    printf("F-Stack server listening on port %d\n", PORT);

    // 初始化 LevelDB
    leveldb_options_t* options = leveldb_options_create();   
    leveldb_options_set_create_if_missing(options, 1);
    char* err = NULL;
    leveldb_t* db = leveldb_open(options, "testdb", &err);
    if (err != NULL) {
        fprintf(stderr, "Failed to open LevelDB: %s\n", err);
        leveldb_free(err);
        return -1;
    }

    leveldb_readoptions_t* read_options = leveldb_readoptions_create();
    leveldb_writeoptions_t* write_options = leveldb_writeoptions_create();

    printf("LevelDB initialized successfully.\n");


    // 創建 kqueue
    kqueue_fd = ff_kqueue();
    if (kqueue_fd < 0) {
        perror("ff_kqueue");
        return -1;
    }
    printf("kqueue_fd: %d, listen_fd: %d\n", kqueue_fd, listen_fd);


    // 將 listen_fd 添加到 kqueue
    struct kevent event;
    EV_SET(&event, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    int ret = ff_kevent(kqueue_fd, &event, 1, NULL, 1, NULL);
    if (ret < 0) {
        perror("ff_kevent: failed to register listen_fd");
        ff_close(listen_fd);
    } else {
        printf("ff_kevent: listen_fd successfully registered\n");
       printf("listen_fd = %d\n" , listen_fd);

    }


    struct event_args args = {
        .listen_fd = listen_fd,
        .db = db,
        .read_options = read_options,
        .write_options = write_options,
    };


    printf("listen_fd = %d\n" , args.listen_fd);

    // 進入事件循環
    ff_run(ff_event_loop, &args);

    // 清理 LevelDB 資源
    leveldb_close(db);
    leveldb_options_destroy(options);

    ff_close(listen_fd);
    return 0;
}   