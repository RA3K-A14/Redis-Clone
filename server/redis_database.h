#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <chrono>

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
    std::unordered_map<std::string, std::unordered_set<std::string>> Set_store;
    std::unordered_map<std::string, std::set<std::pair<double, std::string>>> SSet_store;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiration_map;

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
    bool expire(const std::string &key, const int sec);
    void purgeExpiredKeys();
    
    //List Operations
    std :: vector <std::string> lget(const std::string &key);
    std :: string llen(const std :: string &key);
    void lpush(const std::string &key, const std::string &value);
    void rpush(const std::string &key, const std::string &value);
    std :: string lpop(const std::string &key);
    std :: string rpop(const std::string &key);
    int lrem(const std::string &key, int count, const std::string &value);
    std :: string lindex(const std::string &key, int index);
    void lset(const std::string &key, int index, const std::string &value);
    
    //Hash operations
    void hset(const std::string &key, const std :: vector<std :: pair <std :: string , std :: string>> &fields);
    std :: string hget (const std::string &key, const std::string &field);
    bool hexists (const std::string &key, const std::string &field);
    int hdel (const std::string &key,const std :: vector <std :: string>& fields);
    std :: vector <std :: string> hgetall (const std::string &key);
    std :: vector <std :: string> hkeys (const std::string &key);
    std :: vector <std :: string> hvals (const std::string &key);
    int hlen (const std::string &key);

    //Set Operations
    int sadd (const std::string &key, const std :: vector <std :: string> &members);
    int srem (const std::string &key, const std :: vector <std :: string> &members);
    int sismember (const std::string &key, const std::string &member);
    std :: vector <std :: string> smembers (const std::string &key);
    int scard (const std::string &key);

    //Sorted Set Operations
    int zadd (const std::string &key, const std :: vector<std :: pair <std :: string , std :: string>> &members);
    int zrem (const std::string &key, const std :: vector <std :: string> &members);
    std :: string zscore (const std::string &key, const std::string &member);
    int zrank (const std::string &key, const std::string &member);
    std :: vector <std :: string> zall (const std::string &key);

    // Persistance: Dump/Load the database to/from a file
    bool dump(const std::string &filename);
    bool load(const std::string &filename);
};

#endif