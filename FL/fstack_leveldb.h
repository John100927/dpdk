#ifndef C248F2A9_0B79_4A16_A778_1DBF226DC29D
#define C248F2A9_0B79_4A16_A778_1DBF226DC29D
#ifndef FSTACK_LEVELDB_H
#define FSTACK_LEVELDB_H

#include <leveldb/c.h>

// 初始化 LevelDB
leveldb_t *initialize_leveldb(const char *db_path);

// 處理 F-Stack 的客戶端請求
void process_request(int client_fd, leveldb_t *db);

#endif /* FSTACK_LEVELDB_H */




#endif /* C248F2A9_0B79_4A16_A778_1DBF226DC29D */
