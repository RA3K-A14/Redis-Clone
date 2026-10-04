#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

class RedisDatabase {
private:
    RedisDatabase() = default;
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase&) = delete;
    RedisDatabase& operator = (const RedisDatabase&) = delete;
    std :: mutex DB_mutex;
    std :: unordered_map<std :: string, std :: string> KV_store;
    std :: unordered_map<std :: string, std :: vector <std :: string>> List_store;
    std :: unordered_map<std :: string, std :: unordered_map <std :: string, std :: string>> Hash_store;
public:
    static RedisDatabase& getInstance();

    //Persistance: Dump/Load the database to/from a file
    bool dump(const std :: string& filename);
    bool load(const std :: string& filename);
};

#endif