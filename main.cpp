#include "asiocommunicator.hpp"
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <thread>


using namespace std;
using namespace tea::asiocommunicator;

constexpr int port = 20007;

struct ClientToServer
{
    size_t messageID;
    size_t content;
};

struct ServerToClient
{
    size_t messageID;
    size_t content;
};

void serverProcessMessage(const ClientToServer& input, ServerToClient& output)
{
    output.messageID = input.messageID;
    output.content = input.content*2;
}

int server()
{
    cout<<"I'm a server."<<endl;
    cout<<"Use Ctrl^C to stop me."<<endl;
    boost::asio::io_context context;
    Server srv(context);
    srv.startAccept(port);


    size_t activeSessionCount = 0;
    while(1)
    {
        srv.poll();
        size_t sessionID;
        ClientToServer input;
        //receive messages, process them and send back response
        if(srv.receiveAnySession(sessionID, input))
        {
            std::cout<<"Server received message"<<std::endl;
            ServerToClient output;
            serverProcessMessage(input, output);
            srv.send(sessionID, output);
        }
        //remove finished sessions
        srv.removeFinishedSessions();

        //print new status if status have been changed
        size_t new_activeSessionCount = srv.getActiveSessionsCount();
        if(activeSessionCount != new_activeSessionCount)
        {
            activeSessionCount = new_activeSessionCount;
            std::cout<<"Active session count: "<<activeSessionCount<<std::endl;
        }
    }
    cout<<"Server finished"<<endl;
    return 0;    
}


int client()
{
    cout<<"I'm a client."<<endl;
    Client client;
    client.sync_connect("127.0.0.1", port);
    if(client.getStatus() != Client::Status::connectedOk)
    {
        std::cout<<"Can't connect to remote server"<<std::endl;
        return -1;
    }

    std::cout<<"Sending requests..."<<std::endl;
    for(size_t n = 0; n < 10; n++)
    {
        ClientToServer clientMessage{n, n};
        client.send(clientMessage);
    }

    std::cout<<"Receiving answers..."<<std::endl;
    int response_count = 0;
    while(response_count < 10)
    {
        ServerToClient serverResponse;
        if(client.receive(serverResponse))
        {
            cout<<serverResponse.messageID<<": "<<serverResponse.content<<endl;
            response_count++;
        }
        client.poll();
    }
    cout<<"Client finished"<<endl;
    return 0;
}

int main()
{
    try {
#ifdef SERVER
        return server();
#else
        return client();
#endif
    } catch (boost::system::error_code ec) {
        std::cout<<ec.message()<<std::endl;
    }
}
