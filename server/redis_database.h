#ifndef REDIS_DATABASE_H
#define REDIS_DATABASE_H

#include <string>

class RedisDatabase {
private:
    RedisDatabase() = default;
    ~RedisDatabase() = default;
    RedisDatabase(const RedisDatabase&) = delete;
    RedisDatabase& operator = (const RedisDatabase&) = delete;
public:
    static RedisDatabase& getInstance();

    //Persistance: Dump/Load the database from/to a file
    bool dump(const std :: string& filename);
    bool load(const std :: string& filename);
};

#endif