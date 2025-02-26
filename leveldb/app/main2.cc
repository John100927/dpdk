#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <leveldb/db.h>

#define PORT 9001   // 🔹 Kernel Stack 使用不同的 Port (與 F-Stack 分開)
#define BUFFER_SIZE 1024

void handle_client(int client_fd, leveldb::DB* db) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};

    while (true) {  // **持續處理請求**
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read <= 0) {
            printf("⚠️ Client disconnected.\n");
            snprintf(response, sizeof(response), "👋 Goodbye!\n");
            write(client_fd, response, strlen(response));
            close(client_fd);  // **🔹 只在這裡關閉連線**
            break;
        }

        buffer[bytes_read] = '\0';  // **確保字串結尾**
        std::string input(buffer);
        
        // **解析指令**
        std::string command, key, value;
        std::istringstream iss(input);
        iss >> command;

        if (command == "PUT") {
            if (!(iss >> key >> value)) {
                snprintf(response, sizeof(response), "❌ PUT FAILED: Invalid format\n");
            } else {
                leveldb::WriteOptions write_options;
                leveldb::Status s = db->Put(write_options, key, value);

                if (s.ok()) {
                    // **✅ 印出成功的 Key & Value**
                    // std::cout << "📝 PUT 成功: key=" << key << ", value=" << value << std::endl;
                    snprintf(response, sizeof(response), "✅ PUT SUCCESS: %s\n", key.c_str());
                } else {
                    snprintf(response, sizeof(response), "❌ PUT FAILED: %s\n", s.ToString().c_str());
                }
            }
        } else if (command == "GET") {
            if (!(iss >> key)) {
                snprintf(response, sizeof(response), "❌ GET FAILED: Invalid format\n");
            } else {
                std::string value;
                leveldb::ReadOptions read_options;
                leveldb::Status s = db->Get(read_options, key, &value);

                if (s.ok()) {
                    snprintf(response, sizeof(response), "✅ GET SUCCESS: %s\n", value.c_str());
                    std::cout << "📝 GET 成功: key=" << key << ", value=" << value << std::endl;
                } else {
                    snprintf(response, sizeof(response), "❌ GET FAILED: Key not found\n");
                }
            }
        } else if (command == "EXIT") {
            // **🔹 客戶端請求關閉**
            snprintf(response, sizeof(response), "👋 Goodbye!\n");
            write(client_fd, response, strlen(response));
            printf("🔴 Client requested to close connection.\n");
            close(client_fd);  // **🔹 只在這裡關閉連線**
            break;
        } else {
            snprintf(response, sizeof(response), "❓ Unknown command\n");
        }

        // **回傳結果**
        write(client_fd, response, strlen(response));
    }
}



int main() {
    // **初始化 LevelDB**
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;

    leveldb::Status status = leveldb::DB::Open(options, "/home/oslab/Desktop/john/leveldb/kernel_db", &db);
    if (!status.ok()) {
        std::cerr << "❌ 無法開啟 LevelDB: " << status.ToString() << std::endl;
        return 1;
    }

    // **建立 TCP Server**
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("❌ 無法建立 socket");
        return 1;
    }

    // **設定 socket**
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("❌ 綁定失敗");
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        perror("❌ 監聽失敗");
        return 1;
    }

    std::cout << "✅ Kernel TCP Server 已啟動，監聽端口 " << PORT << std::endl;

    // **接受客戶端請求**
    while (true) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("⚠️ 接受連線失敗");
            continue;
        }
        handle_client(client_fd, db);
    }

    close(server_fd);
    delete db;
    return 0;
}
