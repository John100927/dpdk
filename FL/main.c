#include <stdio.h>
#include <stdlib.h>
#include "fstack_leveldb.h"
#include <ff_api.h>

int main(int argc, char *argv[]) {
    ff_init(argc, argv);

    int server_fd = ff_socket(AF_INET, SOCK_STREAM, 0);

    struct linux_sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = ff_htons(8080),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (ff_bind(server_fd, (struct linux_sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind server\n");
        return 1;
    }

    if (ff_listen(server_fd, 128) < 0) {
        fprintf(stderr, "Failed to listen on server socket\n");
        return 1;
    }

    leveldb_t *db = initialize_leveldb("/tmp/testdb");
    if (!db) {
        fprintf(stderr, "Failed to initialize LevelDB\n");
        return 1;
    }

    while (1) {
        int client_fd = ff_accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            process_request(client_fd, db);
        }
    }

    leveldb_close(db);
    ff_finalize();
    return 0;
}

