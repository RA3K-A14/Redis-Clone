#ifndef HANDLER_H
#define HANDLER_H

#include <string>

class RedisCommandHandler{
private:

public:    
    RedisCommandHandler();
    //Process Command and return RESP-formatted response.
    std :: string formatCommand(const std :: string &commmand_line);
};

#endif