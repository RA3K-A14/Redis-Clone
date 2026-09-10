#include "RedisCommandHandler.h"
#include <vector>
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

    +OK\r\n - simple string
*/

std :: vector <std :: string> parsed_response_command(const std :: string &input){
    std :: vector <std :: string> commands;
    if (input.empty()) return commands;
    //If input doesnt start with * no need to parse commands instead splitting will be done by whitespaces
    if (input[0] != '*') {
        std :: istringstream iss(input);
        std :: string token;
        while (iss >> token)
            commands.push_back(token);
        return commands;
    }
    //Otherwise Parse accordingly
    size_t pos = 1;
    //Carriage return (\r), Line Feed (\n)
    //If crlf is not present dont parse return as is
    size_t crlf = input.find("\r\n",pos);
    if (crlf == std :: string :: npos) return commands;

    int num_of_elements = std :: stoi(input.substr(pos, crlf - pos));
    pos = crlf + 2;

    for (int i = 0; i < num_of_elements; ++i){
        if (pos >= input.size() || input[pos] != '$') break;
        pos ++; // skip '&'

        crlf = input.find("\r\n",pos);
        if (crlf == std :: string :: npos) break;
        int len = std :: stoi(input.substr(pos, crlf - pos));
        pos = crlf + 2;
        
        if (pos + len > input.size()) break;
        std :: string token = input.substr(pos, len);
        commands.push_back(token);
        pos += len + 2; // skin the entire token along with crlf.

    }
    return commands;
}
RedisCommandHandler :: RedisCommandHandler(){}

std :: string RedisCommandHandler :: formatCommand(const std :: string &commmand_line){
    //using RESP parser
    std :: vector <std :: string> commands = parsed_response_command(commmand_line);
    if (commands.empty())   return "~ERROR: Empty commands\r\n";

    //Handle commands based on commands[0]- Actual Commands
    //And commmands[1..] - Arguments
    std :: string cmd  = commands[0];
    std :: transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    std :: ostringstream response;

    //Connect to Database

    //Check Commands

    return response.str();
}