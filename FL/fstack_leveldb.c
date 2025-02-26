#include "fstack_leveldb.h"
#include <ff_api.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

leveldb_t *initialize_leveldb(const char *db_path) {
    char *err = NULL;
    leveldb_options_t *options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);

    leveldb_t *db = leveldb_open(options, db_path, &err);
    if (err != NULL) {
        fprintf(stderr, "Error opening LevelDB: %s\n", err);
        leveldb_free(err);
        return NULL;
    }

    leveldb_options_destroy(options);
    return db;
}

void process_request(int client_fd, leveldb_t *db) {
    char buffer[1024];
    ssize_t len = ff_read(client_fd, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        ff_close(client_fd);
        return;
    }

    buffer[len] = '\0';

    if (strncmp(buffer, "PUT", 3) == 0) {
        char *key = strtok(buffer + 4, " ");
        char *value = strtok(NULL, "\n");

        char *err = NULL;
        leveldb_put(db, NULL, key, strlen(key), value, strlen(value), &err);
        if (err != NULL) {
            ff_write(client_fd, "ERROR\n", 6);
            leveldb_free(err);
        } else {
            ff_write(client_fd, "OK\n", 3);
        }
    } else if (strncmp(buffer, "GET", 3) == 0) {
        char *key = buffer + 4;

        char *err = NULL;
        size_t read_len;
        char *value = leveldb_get(db, NULL, key, strlen(key), &read_len, &err);
        if (value) {
            ff_write(client_fd, value, read_len);
            free(value);
        } else {
            ff_write(client_fd, "NOT FOUND\n", 10);
        }
    } else {
        ff_write(client_fd, "INVALID COMMAND\n", 17);
    }

    ff_close(client_fd);
}
