#include "redis_database.h"

#include <fstream>
#include <sstream>
#include <string>

RedisDatabase &RedisDatabase::getInstance()
{
    static RedisDatabase instance;
    return instance;
}

void RedisDatabase::flushALL()
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    KV_store.clear();
    List_store.clear();
    Hash_store.clear();
}

// Key-Value operations
void RedisDatabase::set(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    KV_store[key] = value;
}

std::string RedisDatabase::get(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = KV_store.find(key);
    if (it != KV_store.end())
        return "$" + std::to_string((it->second).size()) + "\r\n" + (it->second) + "\r\n";
    return "$-1\r\n";
}

std::vector<std::string> RedisDatabase::keys()
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std::vector<std::string> allKeys;
    for (const auto &pair : KV_store)
    {
        allKeys.push_back(pair.first);
    }
    for (const auto &pair : List_store)
    {
        allKeys.push_back(pair.first);
    }
    for (const auto &pair : Hash_store)
    {
        allKeys.push_back(pair.first);
    }
    return allKeys;
}

std::string RedisDatabase::type(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    if (KV_store.find(key) != KV_store.end())
        return "string";
    if (List_store.find(key) != List_store.end())
        return "list";
    if (Hash_store.find(key) != Hash_store.end())
        return "hash";
    return "none";
}

bool RedisDatabase::del(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    bool erased = false;
    erased = KV_store.erase(key) > 0;
    erased = List_store.erase(key) > 0;
    erased = Hash_store.erase(key) > 0;
    return erased;
}

bool RedisDatabase::rename(const std::string &oldKey, const std::string &newKey)
{
    std::lock_guard<std::mutex> lock(DB_mutex);

    if (KV_store.find(oldKey) != KV_store.end())
    {
        KV_store[newKey] = (KV_store.find(oldKey))->second;
        KV_store.erase(oldKey);
        return true;
    }

    if (List_store.find(oldKey) != List_store.end())
    {
        List_store[newKey] = (List_store.find(oldKey))->second;
        List_store.erase(oldKey);
        return true;
    }

    if (Hash_store.find(oldKey) != Hash_store.end())
    {
        Hash_store[newKey] = (Hash_store.find(oldKey))->second;
        Hash_store.erase(oldKey);
        return true;
    }

    return false;
}
// List operations
// Hash operations

// Memory -> File -dump()

bool RedisDatabase::dump(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs)
        return false;

    for (const auto &kv : KV_store)
    {
        ofs << "K " << kv.first << " " << kv.second << "\n";
    }
    for (const auto &kl : List_store)
    {
        ofs << "L " << kl.first;
        for (const auto &item : kl.second)
            ofs << " " << item;
        ofs << "\n";
    }
    for (const auto &kh : Hash_store)
    {
        ofs << "H " << kh.first;
        for (const auto &field_val : kh.second)
            ofs << " " << field_val.first << ":" << field_val.second;
        ofs << "\n";
    }
    return true;
}

// File -> Memory -load()
bool RedisDatabase::load(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs)
        return false;

    KV_store.clear();
    List_store.clear();
    Hash_store.clear();

    std::string line;
    while (std::getline(ifs, line))
    {
        std::istringstream iss(line);
        char type;
        iss >> type;
        if (type == 'K')
        {
            std::string key, value;
            iss >> key >> value;
            KV_store[key] = value;
        }
        else if (type == 'L')
        {
            std::string key;
            iss >> key;
            std::vector<std::string> list;
            std::string item;
            while (iss >> item)
                list.push_back(item);
            List_store[key] = list;
        }
        else if (type == 'H')
        {
            std::string key;
            iss >> key;
            std::unordered_map<std::string, std::string> hash;
            std::string pair;
            while (iss >> pair)
            {
                auto pos = pair.find(':');
                if (pos != std::string::npos)
                {
                    std::string field = pair.substr(0, pos);
                    std::string value = pair.substr(pos + 1);
                    hash[field] = value;
                }
            }
            Hash_store[key] = hash;
        }
    }
    return true;
}