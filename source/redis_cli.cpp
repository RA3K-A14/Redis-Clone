#include <iostream>

int main(int argc, char *argv[]){
    std :: string host = "127.0.0.1"; //127.0.0.1 points to current device host address 
    int port = 6379; // default port value is universally 6379
    
    //Parsing command line arguments for -h(host) and -p(port) 
    for (int i = 1; i < argc; ++i){
        std :: string argument = argv[i];
        //if suppose you enter ./redis_cli -h x.x.x.x, update host to x.x.x.x (default: 127.0.0.1)
        //else if ./redis_cli -p xxxx, set port to xxxx (default : 6379)
        if(argument == "-h" && i + 1 < argc){
            host = argv[++i];
        }
        else if (argument == "-p" && i + 1 < argc){
            port = std :: stoi(argv[++i]);
        }
        else {
            break;
        }
    }
    return 0;
}