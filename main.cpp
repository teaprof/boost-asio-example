// MIT License
//
// Copyright (c) 2023 Egor Tsvetkov
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "asiocommunicator.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>


using tea::asiocommunicator::Server, tea::asiocommunicator::Client;

constexpr int port = 20007;
constexpr int message_count = 10;

struct ClientToServerMsg
{
    size_t message_id{};
    size_t content{};
};

struct ServerToClientMsg
{
    size_t message_id{};
    size_t content{};
};

void serverProcessMessage(const ClientToServerMsg& input, ServerToClientMsg& output)
{
    output.message_id = input.message_id;
    output.content = input.content*2;
}

int server()
{
    std::cout<<"I'm a server."<<std::endl;
    std::cout<<"Use Ctrl^C to stop me."<<std::endl;
    boost::asio::io_context context;
    Server srv(context);
    srv.startAccept(port);


    size_t active_session_count = 0;
    while(true)
    {
        srv.poll();
        size_t session_id{};
        ClientToServerMsg input;
        //receive messages, process them and send back response
        if(srv.receiveAnySession(session_id, input))
        {
            std::cout<<"Server received message"<<std::endl;
            ServerToClientMsg output;
            serverProcessMessage(input, output);
            srv.send(session_id, output);
        }
        //remove finished sessions
        srv.removeFinishedSessions();

        //print new status if status have been changed
        const size_t active_session_count_new = srv.getActiveSessionsCount();
        if(active_session_count != active_session_count_new)
        {
            active_session_count = active_session_count_new;
            std::cout<<"Active session count: "<<active_session_count<<std::endl;
        }
    }
    std::cout<<"Server finished"<<std::endl;
    return 0;    
}


int client()
{
    std::cout<<"I'm a client."<<std::endl;
    Client client;
    client.sync_connect("127.0.0.1", port);
    if(client.getStatus() != Client::Status::connectedOk)
    {
        std::cout<<"Can't connect to remote server"<<std::endl;
        return -1;
    }

    std::cout<<"Sending requests..."<<std::endl;
    for(size_t message_id = 0; message_id < message_count; message_id++)
    {
        auto message_content = message_id;
        const ClientToServerMsg client_message{message_id, message_content};
        client.send(client_message);
    }

    std::cout<<"Receiving answers..."<<std::endl;
    int response_count = 0;
    while(response_count < message_count)
    {
        ServerToClientMsg server_response;
        if(client.receive(server_response))
        {
            std::cout<<server_response.message_id<<": "<<server_response.content<<std::endl;
            response_count++;
        }
        client.poll();
    }
    std::cout<<"Client finished"<<std::endl;
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
    } catch (const boost::system::error_code& ec) {
        std::cout<<ec.message()<<std::endl;
    } catch (...) {
        std::cout<<"Unhandled exception"<<std::endl;
    }
}
