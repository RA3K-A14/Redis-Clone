# Redis-Clone
A lightweight Redis-like in-memory database written in c++
This project is built to understand working with TCP client/server communication, RESProtocol parsing, multiple data types, concurrency, persistence, key expiration and visible demonstration of data structures andalgorithms.

## Problem Statement
Modern applications require fast and efficient data access with low latency and support for multiple concurrent clients.
Systems such as cache memory or Redis in-memory database provides such capabilities through an in-memory key-value architecture, multiple hash table stores, networking, persistance, expiration, etc.

## Description
This Project aims to develop a lightweight Redis-like database from scratch in c++, implementing the core concepts of an in-memory data store while implementing concepts from Data Structures, Object oriented programming, Networking, and File Handling.
It provides common redis commands over a plain TCP socket using the RESProtocol.

Database commands are as follows:-
  - Common Commands:
    + PING -> This command is used mainly to test the connection is alive, it returns PONG.
    + ECHO "Message" -> This command returns the message, it used to test the response handling of the server/client.
    + FLUSHALL -> This commands is used to cleanup all key data stores.
    + SAVE -> This command dumps the entire memory store to a local file.
    + LOAD -> This command loads the entire memory store from a local file.
  - Expiration & TTL:
    + EXPIRE key seconds -> set TTL for the given key to be erased upon expiration.
    + TTL key -> returns the time-to-live(remainig life time) of the key.
    + PERSIST key -> removes the expiration of the key.
  - Key/Value:
    + SET key value -> This commands stores a key value pair in string format
    + GET key -> retrieves the string or nil
    + KEYS -> List all keys
    + TYPE key -> returns the type of key stored string/list/hash/set/sorted set.
    + DEL key -> erases the key present in any/all stores.
    + RENAME oldKey newKey -> rename a key
  - Lists:
    + LGET key -> returns the elements stored
    + LLEN key -> returns the number of elements stored
    + LPUSH/RPUSH key element [elements...] -> stores one/multiple elements to left/right end of the list.
    + LPOP/RPOP key -> returns element from left/right end of the list.
    + LREM key count element -> erases the count of elements from the list and return the number of elements removed.
    + LINDEX key index -> returns the element present at the index.
    + LSET key index value -> resets the value of element stored at the index
  - Hash:
    + HSET key field value [field value ...] -> sets one or more field:value pair to hash data store. 
    + HGET key field -> returns the value stored at field, key
    + HEXISTS key field -> checks the existance of a key, field
    + HDEL key field [field ...] -> erases one or more fields from the hash stored at key
    + HLEN key -> returns the number of field:value pairs stored
    + HKEYS key -> returns bulk strings of fields stored
    + HVALS key -> returns bulk strings of values stored
    + HGETALL key -> returns both fields and values
  - Set:
    + SADD key member -> stores member/s in a set
    + SREM key member -> removes member/s from a set
    + SISMEMBER key member -> checks if a member is in that set
    + SMEMBERS key -> returns all members in that set
    + SCARD key -> return the number of members in that set
  - Sorted Set:
    + ZADD key score member -> adds member/s with their score to the sorted set
    + ZREM key member -> removed the member/s along with their score from the sorted set
    + ZSCORE key member -> return the score of the member
    + ZRANK key member -> returns the rank(index) of the member
    + ZALL key -> returns all score : members present in that sorted set
   
## Design & Architecture:
- Concurrency: Each client is handled in it's own std::thread.
- Synchronization: A single std::mutex guards all access to in-memory stores.
- RESP parsing: custom parsing done in RedisCommandHandler supporting both inline and array formats.
- Data stores implemented:
  + KV_store- mapping <string, string> for strings
  + List_store- mapping <string, vector<string>> for lists
  + Hash_store- mapping <string, unordered_map<string,string>> for hashes
  + Set_store- mapping <string, unordered_set<string>> for sets
  + SSet_store- mapping <string, set<pair<double, string>>> for sorted sets(pairs are sorted according to their score, matching score pairs are sorted lexicographically.)
- Expiration: Lazy expiration is implemented via purgeExpiredKeys() along with TTL map expiration_map.
- Persistance: text-based simple load/dump in redisDB.rdb
