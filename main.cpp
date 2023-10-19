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
        if(srv.receiveAnySession(sessionID, input))
        {
            std::cout<<"Server received message"<<std::endl;
            ServerToClient output;
            serverProcessMessage(input, output);
            srv.send(sessionID, output);
        }
        srv.removeFinishedSessions();

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
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
    cout<<"I'm a client."<<endl;
    Client client;    
    while(client.getStatus() != Client::Status::connectedOk)
    {
        client.sync_connect("127.0.0.1", port);
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
