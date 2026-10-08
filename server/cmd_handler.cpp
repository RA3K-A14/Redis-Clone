#include "cmd_handler.h"
#include "redis_database.h"

#include <algorithm>
#include <sstream>

/*
    RESP Parsing:
    RESP is essentially a serialization protocol that supports several data types. In RESP, the first byte of data determines its type.

    The \r\n (CRLF) is the protocol's terminator, which always separates its parts.

    *2\r\n$4\r\nPING\r\n$4\r\nTEST\r\n
    *2-> array has 2 elements
    $4-> bulk string of 4 characters
    PING
    TEST

    +OK\r\n - (+) denotes simple string
*/

std::vector<std::string> parsed_response_command(const std::string &input)
{
    std::vector<std::string> commands;
    if (input.empty())
        return commands;
    // If input doesnt start with * no need to parse commands instead splitting will be done by whitespaces
    if (input[0] != '*')
    {
        std::istringstream iss(input);
        std::string token;
        while (iss >> token)
            commands.push_back(token);
        return commands;
    }
    // Otherwise Parse accordingly
    size_t pos = 1;
    // Carriage return (\r), Line Feed (\n)
    // If crlf is not present dont parse return as is
    size_t crlf = input.find("\r\n", pos);
    if (crlf == std::string::npos)
        return commands;

    int num_of_elements = std::stoi(input.substr(pos, crlf - pos));
    pos = crlf + 2;

    for (int i = 0; i < num_of_elements; ++i)
    {
        if (pos >= input.size() || input[pos] != '$')
            break;
        pos++; // skip '&'

        crlf = input.find("\r\n", pos);
        if (crlf == std::string::npos)
            break;
        int len = std::stoi(input.substr(pos, crlf - pos));
        pos = crlf + 2;

        if (pos + len > input.size())
            break;
        std::string token = input.substr(pos, len);
        commands.push_back(token);
        pos += len + 2; // skin the entire token along with crlf.
    }
    return commands;
}
RedisCommandHandler::RedisCommandHandler() {}

std::string RedisCommandHandler::processCommand(const std::string &commmand_line)
{
    // using RESP parser
    std::vector<std::string> commands = parsed_response_command(commmand_line);
    if (commands.empty())
        return "-ERR Empty commands\r\n";

    // Handle commands based on commands[0]- Actual Commands
    // And commmands[1..] - Arguments
    std::string cmd = commands[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    // Connect to Database
    RedisDatabase &db = RedisDatabase::getInstance();

    // Check Commands
    if (cmd == "COMMAND")
    {
        return "+OK\r\n";
    }
    else if (cmd == "PING")
    {
        return "+PONG\r\n";
    }
    else if (cmd == "ECHO")
    {
        if (commands.size() < 2)
        {
            return "-ERR Echo requires a message\r\n";
        }
        return "+" + commands[1] + "\r\n";
    }
    else if (cmd == "FLUSHALL")
    {
        db.flushALL();
        return "+OK\r\n";
    }
    // Key-Value operations
    else if (cmd == "SET")
    {
        if (commands.size() < 3)
            return "-ERR SET requires key and value\r\n";
        db.set(commands[1], commands[2]);
        return "+OK\r\n";
    }
    else if (cmd == "GET")
    {
        if (commands.size() < 2)
            return "-ERR GET requires key\r\n";
        std :: string val = db.get(commands[1]);
        if (val.empty())
            return "-1\r\n";
        return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
    }
    else if (cmd == "KEYS")
    {
        std::vector<std::string> allKeys = db.keys();
        std::ostringstream oss;
        oss << "*" << allKeys.size() << "\r\n";
        for (const auto &key : allKeys)
            oss << "$" << key.size() << "\r\n" << key << "\r\n";
        return oss.str();
    }
    else if (cmd == "TYPE")
    {
        if (commands.size() < 2)
            return "-ERR TYPE requires key\r\n";
        std::string type = db.type(commands[1]);
        return "+" + type + "\r\n";
    }
    else if (cmd == "DEL")
    {
        if (commands.size() < 2)
            return "-ERR DEL requires key\r\n";
        if (db.del(commands[1]))
            return ":1\r\n";
        return ":0\r\n";
    }
    else if (cmd == "EXPIRE")
    {
        if (commands.size() < 3)
            return "-ERR EXPIRE requires key and time in seconds\r\n";
        else
            return "+OK\r\n";
    }
    else if (cmd == "RENAME")
    {
        if (commands.size() < 3)
            return "-ERR RENAME requires old key name and new key name\r\n";
        if (db.rename(commands[1], commands[2]))
            return "+OK\r\n";
        return "-ERR Key not found or rename failed\r\n";
    }
    // List operations
    else if (cmd == "LGET")
    {
        if (commands.size() < 2)
            return "-ERR LGET requires a key.\r\n";
        auto allElem = db.lget(commands[1]);
        std :: ostringstream oss;
        oss << "*" << allElem.size() << "\r\n";
        for (const auto& elem : allElem)
        {
            oss << "$" << elem.length() << "\r\n" << elem << "\r\n";
        }
        return oss.str();
    }
    else if (cmd == "LLEN")
    {
        if (commands.size() < 2)
            return "-ERR LLEN requires a key.\r\n";
        std :: string len = db.llen(commands[1]);
        return ":" + len + "\r\n";
    }
    else if (cmd == "LPUSH")
    {
        if (commands.size() < 3)
            return "-ERR LPUSH requires a key and atleast one element.\r\n";
        for (size_t i = 2; i < commands.size(); ++i)
        {
            db.lpush(commands[1],commands[i]);
        }
        auto len = db.llen(commands[1]);
        return ":" + len + "\r\n";
    }
    else if (cmd == "RPUSH")
    {
        if (commands.size() < 3)
            return "-ERR RPUSH requires a key and atleast one element.\r\n";
        for (size_t i = 2; i < commands.size(); ++i)
        {
            db.rpush(commands[1],commands[i]);
        }
        auto len = db.llen(commands[1]);
        return ":" + len + "\r\n";
    }
    else if (cmd == "LPOP")
    {
        if (commands.size() < 2)
            return "-ERR LPOP requires a key.\r\n";
        std :: string val = db.lpop(commands[1]);
        if (val.empty())
            return "-1\r\n";
        return "$" + std :: to_string(val.size()) + "\r\n" + val + "\r\n";
    }
    else if (cmd == "RPOP")
    {
        if (commands.size() < 2)
            return "-ERR RPOP requires a key.\r\n";
        std :: string val = db.rpop(commands[1]);
        if (val.empty())
            return "-1\r\n";
        return "$" + std :: to_string(val.size()) + "\r\n" + val + "\r\n";
    }
    else if (cmd == "LREM")
    {
        if (commands.size() < 4)
            return "-ERR RPOP requires a key, count and element.\r\n";
        try
        {
            int count = std :: stoi (commands[2]);
            int removed = db.lrem(commands[1],count,commands[3]);
            return ":" + std::to_string(removed) + "\r\n";
        }
        catch (const std :: exception&)
        {
            return "-ERR Invalid count";
        }
    }
    else if (cmd == "LINDEX")
    {
        if (commands.size() < 3)
            return "-ERR RPOP requires a key and index.\r\n";
        try
        {
            int index = std :: stoi (commands[2]);
            std :: string val = db.lindex(commands[1], index);
            if (val.empty())
                return "-1\r\n";
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        catch(const std :: exception&)
        {
            return "-ERR Invalid index";
        }
    }
    else if (cmd == "LSET")
    {
        if (commands.size() < 4)
            return "-ERR RPOP requires a key, index and value.\r\n";
        try
        {
            int index = std::stoi(commands[2]);
            db.lset(commands[1],index,commands[3]);
            return "+OK\r\n";
        }
        catch(std::exception&)
        {
            return "-ERR Invalid index";
        }
    }
    // Hash operations
    else if (cmd == "HSET")
    {
        if (commands.size() < 4)
            return "-ERR HSET requires a key, field and value.\r\n";
        if ((commands.size() - 2) % 2 != 0)
            return "-ERR Wrong number of arguements.\r\n";
        std :: vector<std :: pair <std :: string , std :: string>> fields;
        for (size_t i = 2; i < commands.size(); i += 2){
            fields.push_back({commands[i], commands[i + 1]});
        }
        db.hset(commands[1],fields);
        return "+OK\r\n";
    }
    else if (cmd == "HGET")
    {
        if (commands.size() < 3)
            return "-ERR HGET requires a key and field.\r\n";
        std :: string response = db.hget(commands[1],commands[2]);
        if (response.empty())
            return "-1\r\n";
        return "$" + std::to_string(response.length()) + "\r\n" + response + "\r\n";
    }
    else if (cmd == "HEXISTS")
    {
        if (commands.size() < 3)
            return "-ERR HEXISTS requires a key and field.\r\n";
        if (db.hexists(commands[1],commands[2]))
            return ":1\r\n";
        else
            return ":0\r\n";
    }
    else if (cmd == "HDEL")
    {
        if (commands.size() < 3)
            return "-ERR HDEL requires a key and field.\r\n";
        std :: vector <std :: string> fields;
        for (size_t i = 2; i < commands.size(); ++i)
        {
            fields.push_back(commands[i]);
        }
        int removed = db.hdel(commands[1], fields);
        return ":" + std::to_string(removed) + "\r\n";
    }
    else if (cmd == "HGETALL")
    {
        if (commands.size() < 2)
            return "-ERR HGETALL requires a key.\r\n";
        std :: vector <std :: string> allpairs = db.hgetall(commands[1]);
        std :: ostringstream oss;
        oss << "*" << allpairs.size() << "\r\n";
        for (const auto& pair : allpairs)
            oss << "$" << pair.size() << "\r\n" << pair << "\r\n";
        return oss.str();
    }
    else if (cmd == "HKEYS")
    {
        if (commands.size() < 2)
            return "-ERR HKEYS requires a key.\r\n";
        std :: vector <std :: string> allFields = db.hkeys(commands[1]);
        std :: ostringstream oss;
        oss << "*" << allFields.size() << "\r\n";
        for (const auto& field : allFields)
            oss << "$" << field.size() << "\r\n" << field << "\r\n";
        return oss.str();
    }
    else if (cmd == "HVALS")
    {
        if (commands.size() < 2)
            return "-ERR HVALS requires a key.\r\n";
        std :: vector <std :: string> allVals = db.hvals(commands[1]);
        std :: ostringstream oss;
        oss << "*" << allVals.size() << "\r\n";
        for (const auto& val : allVals)
            oss << "$" << val.size() << "\r\n" << val << "\r\n";
        return oss.str();
    }
    else if (cmd == "HLEN")
    {
        if (commands.size() < 2)
            return "-ERR HLEN requires a key.\r\n";
        int len = db.hlen(commands[1]);
        return ":" + std::to_string(len) + "\r\n";
    }
    //Set operations
    else if (cmd == "SADD")
    {
        if (commands.size() < 3)
            return "-ERR SADD requires a key and a member.";
        std :: vector <std :: string> members;
        for(size_t i = 2; i < commands.size(); ++i)
        {
            members.push_back(commands[i]);
        }
        int num_of_elements_added = db.sadd(commands[1], members);
        return ":" + std::to_string(num_of_elements_added) + "\r\n";
    }
    else if (cmd == "SREM")
    {
        if (commands.size() < 3)
            return "-ERR SREM requires a key and members.";
        std :: vector <std :: string> members;
        for(size_t i = 2; i < commands.size(); ++i)
        {
            members.push_back(commands[i]);
        }
        int removed = db.srem(commands[1], members);
        return ":" + std::to_string(removed) + "\r\n";
    }
    //Sorted Set operations
    // Default response
    else
    {
        return "-ERR Unknown command\r\n";
    }
}
