#ifndef HANDLER_H
#define HANDLER_H

#include <string>
#include <vector>

class RedisCommandHandler{
private:

public:    
    RedisCommandHandler();
    //Process Command and return RESP-formatted response.
    std :: string formatCommand(const std :: string &commmand_line);
};

std :: vector <std :: string> parsed_response_command(const std :: string &input);

#endif