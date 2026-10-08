#include "redis_database.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iterator>

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
        return it -> second;
    return std :: string();
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

bool RedisDatabase::expire(const std::string &key, const int sec) {}

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
std :: vector <std::string> RedisDatabase::lget(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it != List_store.end())
    {
        return it -> second;
    }
    return {};
}

std :: string RedisDatabase :: llen(const std :: string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it != List_store.end())
    {
        std :: ostringstream oss; 
        oss << (it -> second).size();
        return oss.str();
    }
    return std::string("0");
}

void RedisDatabase :: lpush(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    List_store[key].insert(List_store[key].begin(), value);
}

void RedisDatabase :: rpush(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    List_store[key].push_back(value);
}

std :: string RedisDatabase :: lpop(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it == List_store.end() && it -> second.empty())
        return std :: string();
    std :: string val = it -> second.front();
    it -> second.erase(it -> second.begin());
    return val;
}

std :: string RedisDatabase :: rpop(const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it == List_store.end() && it -> second.empty())
        return std :: string();
    std :: string val = it -> second.back();
    it -> second.pop_back();
    return val;
}

int RedisDatabase :: lrem(const std::string &key, int count, const std::string &value)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it == List_store.end() && it -> second.empty())
        return 0;
    int removed = 0;
    auto& list = it -> second;
    if (count == 0)
    {
        auto newEnd = std :: remove(list.begin(),list.end(), value);
        removed = std :: distance(newEnd, list.end());
        list.erase(newEnd, list.end());
    }
    else if (count > 0)
    {
        auto itr = list.begin();
        while(itr != list.end() && removed < count)
        {
            if (*itr == value) 
            {
                itr = list.erase(itr);
                removed++;
            }
            else 
            {
                itr++;
            }
        }
    }
    else
    {
        auto itr = list.end();
        while(itr != list.begin() && removed < (-count))
        {
            --itr;
            if (*itr == value) 
            {
                itr = list.erase(itr);
                removed++;
            }
            else 
            {
                itr--;
            }
        }
    }
    return removed;
}

std :: string RedisDatabase :: lindex(const std::string &key, int index)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if (it == List_store.end())
        return std::string();
    int size = (it->second).size();
    if (index < 0)
    {
        index = size + index;
    }
    if (index >= size || index <= (-size))
        return std::string();
    return (it->second)[index];
}

void RedisDatabase :: lset(const std::string &key, int index, const std::string &value)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    auto it = List_store.find(key);
    if(it == List_store.end())
        return;
    auto& list = it->second;
    int size = list.size();
    if (index < 0)
        index = size + index;
    if (index <= (-size) || index >= (size))
        return;
    list[index] = value;
}

// Hash operations

void RedisDatabase :: hset(const std::string &key, const std :: vector<std :: pair <std :: string , std :: string>> &fields) 
{
    std::lock_guard<std::mutex> lock(DB_mutex);

    for (size_t i = 0; i < fields.size(); ++i)
    {
        Hash_store[key][fields[i].first] = fields[i].second;
    }
}

std :: string RedisDatabase :: hget (const std::string &key, const std::string &field)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    if (Hash_store.find(key) == Hash_store.end() || Hash_store[key].find(field) == Hash_store[key].end())
        return std::string();
    return Hash_store[key][field];
}

bool RedisDatabase :: hexists (const std::string &key, const std::string &field)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    if (Hash_store.find(key) == Hash_store.end() || Hash_store[key].find(field) == Hash_store[key].end())
        return false;
    return true;
}

int RedisDatabase :: hdel (const std::string &key,const std :: vector <std :: string>& fields)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    if (Hash_store.find(key) == Hash_store.end())
        return 0;
    int removed = 0;
    for (auto field : fields)
    {
        if (Hash_store[key].find(field) != Hash_store[key].end())
        {
            Hash_store[key].erase(field);
            removed++;
        }
    }
    return removed;
}

std :: vector <std :: string> RedisDatabase :: hgetall (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std :: vector <std :: string> allPairs;
    if (Hash_store.find(key) == Hash_store.end())
        return {};
    auto& fvs = Hash_store[key];
    for (const auto& fv : fvs)
    {
        allPairs.push_back(fv.first);
        allPairs.push_back(fv.second);
    }
    return allPairs;
}

std :: vector <std :: string> RedisDatabase :: hkeys (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std :: vector <std :: string> allFields;
    if (Hash_store.find(key) == Hash_store.end())
        return {};
    auto& fvs = Hash_store[key];
    for (const auto& fv : fvs)
    {
        allFields.push_back(fv.first);
    }
    return allFields;
}

std :: vector <std :: string> RedisDatabase :: hvals (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    std :: vector <std :: string> allVals;
    if (Hash_store.find(key) == Hash_store.end())
        return {};
    auto& fvs = Hash_store[key];
    for (const auto& fv : fvs)
    {
        allVals.push_back(fv.second);
    }
    return allVals;
}

int RedisDatabase :: hlen (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    if (Hash_store.find(key) == Hash_store.end())
        return 0;
    return Hash_store[key].size();
}

//Set Operations

int RedisDatabase :: sadd(const std::string &key, const std :: vector <std :: string> &members)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    int added = 0;
    for(const auto& member : members)
    {
        if(Set_store[key].insert(member).second)
        {
            added++;
        }
    }
    return added;
}

int RedisDatabase :: srem(const std::string &key, const std :: vector <std :: string> &members)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    purgeExpiredKeys();
    if (Set_store.find(key) == Set_store.end())
        return 0;
    int removed = 0;
    for(const auto& member : members)
    {
        removed += (Set_store[key].erase(member));
    }
    return removed;
}

int RedisDatabase :: sismember(const std::string &key, const std::string &member)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    purgeExpiredKeys();
    if(Set_store.find(key) == Set_store.end())
        return 0;
    if(Set_store[key].find(member) == Set_store[key].end())
        return 0;
    return 1;
}

std :: vector <std :: string> RedisDatabase :: smembers (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    purgeExpiredKeys();
    if (Set_store.find(key) == Set_store.end())
        return {};
    std :: vector <std :: string> allMembers;
    const auto &it = Set_store.at(key);
    for (const auto &s : it)
    {
        allMembers.push_back(s);
    }
    return allMembers;
}

int RedisDatabase :: scard (const std::string &key)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    purgeExpiredKeys();
    if (Set_store.find(key) == Set_store.end())
        return 0;
    int len = Set_store[key].size();
    return len;
}

//Sorted Set Operations

int RedisDatabase :: zadd(const std::string &key, const std :: vector<std :: pair <std :: string , std :: string>> &members)
{
    std::lock_guard<std::mutex> lock(DB_mutex);
    int added = 0;
    for(const auto& item : members)
    {
        double score;
        try{
            score = std::stod(item.first);
        }catch(std::exception&)
        {
            return -1;
        }
        std::string member = item.second;
        bool found = false;
        for(auto it = SSet_store[key].begin(); it != SSet_store[key].end(); ++it)
        {
            if (member == it->second)
            {
                SSet_store[key].erase(it);
                SSet_store[key].insert({score, member});
                found = true;
                break;
            }
        }
        if (!found)
        {
            SSet_store[key].insert({score, member});
            added++;
        }
    }
    return added;
}

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
