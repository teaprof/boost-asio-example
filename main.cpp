#include "asiocommunicator.hpp"
#include <unistd.h>
#include <iostream>


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
    cout<<"I'm a server"<<endl;
    Server srv(port);
    srv.startAccept();
    cout<<"Server finished"<<endl;

    while(true)
    {
        srv.poll();
        size_t sessionID;
        ClientToServer input;
        if(srv.receiveAnySession(sessionID, input))
        {
            ServerToClient output;
            serverProcessMessage(input, output);
            srv.sendcopy(sessionID, output);
        }
    };
    return 0;
}


int client()
{
    cout<<"I'm a client"<<endl;
    cout<<"Client finished"<<endl;
    Client client;
    client.connect("127.0.0.1", port);
    Buffer inputbuf;
    for(size_t n = 0; n < 10; n++)
    {
        Buffer outbuf(sizeof(ClientToServer));
        ClientToServer *output = reinterpret_cast<ClientToServer*>(outbuf.data());
        output->messageID = n;
        output->content = n;
        client.send(outbuf);
        while(client.receive(inputbuf) == false)
            client.poll();
        ServerToClient* result = reinterpret_cast<ServerToClient*>(inputbuf.data());
        cout<<result->messageID<<": "<<result->content<<endl;
    }
    return 0;
}

int main()
{
    int pid = fork();
    if(pid == 0)
    {
        return server();
    } else {
        return client();
    }
}
