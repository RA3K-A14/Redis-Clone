#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

class RedisDatabase
{
private:
    RedisDatabase() = default;
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase &) = delete;
    RedisDatabase &operator=(const RedisDatabase &) = delete;
    std::mutex DB_mutex;
    std::unordered_map<std::string, std::string> KV_store;
    std::unordered_map<std::string, std::vector<std::string>> List_store;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> Hash_store;

public:
    static RedisDatabase &getInstance();

    void flushALL();
    // Key-Value Operations
    void set(const std::string &key, const std::string &value);
    std::string get(const std::string &key);
    std::vector<std::string> keys();
    std::string type(const std::string &key);
    bool del(const std::string &key);
    bool rename(const std::string &oldKey, const std::string &newKey);
    // bool expire(const std :: string& key);

    // Persistance: Dump/Load the database to/from a file
    bool dump(const std::string &filename);
    bool load(const std::string &filename);
};

#endif