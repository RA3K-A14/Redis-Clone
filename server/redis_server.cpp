#include <iostream>
#include <thread>
#include <chrono>
#include "server.h"

int main(int argc, char *argv[]){
    int port = 6379;
    if(argc >= 2) port = std::stoi(argv[1]); // stoi - converts string to signed integer
    // Sets port to a specific port if argument count is greater than 2 else default port is 6379.
    
    RedisServer server(port);

    /*
    //Background persistance:
    std :: thread persistance_thread([](){
        while (true){
            std :: this_thread :: sleep_for(std :: chrono :: seconds(300));
            //dump the database 
        }
    });
    persistance_thread.detach();
    */

    server.run();
    return 0;
}