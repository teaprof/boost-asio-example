# Intro

## What is this? 

This is a yet another example of how to use C++ `boost::asio` library. This code can be used as a starting point to build you own project based on boost::asio. 

Original examples of using boost::asio library can be found at [official site](https://www.boost.org/doc/libs/master/doc/html/boost_asio/examples.html).

## What is boost::asio?

[boost::asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio.html) (acronym for ASynchronous Input/Output) is a famous C++ library which contains a well-designed set of classes and functions for network operations. 

Documentation on boost::asio can be found [here](https://beta.boost.org/doc/libs/develop/doc/html/boost_asio.html).

## Motivation
Every time I started a new project, I took one of the `boost::asio` examples as a starting point and added many features to use in my project. The majority of these features are related to the application-level logic. This always includes development of the server and client classes that can send and receive some application-specific data structures. After some such tries, I decided to prepare my own example (the present code) of how to use `boost::asio` library that already contains these features. The present code can be the starting point for new your projects.

Original `boost::asio` examples and tutorials are concise. This is good for learning boost::asio library and for understanding how it works. In complex real-life projects `boost::asio` calls should be separated from business logic. The present project is a bridge between low-level `boost::asio` functions and high-level business logic of your project

# Example

First, we need to include the required header files and declare which namespaces we are going to use:

```
#include <tea/asiocommunicator.hpp>
#include <iostream>

using tea::asiocommunicator::Server;
using tea::asiocommunicator::Client;
```

Next, we declare the port that the server should listen to:

```
constexpr int port = 20007;
constexpr int message_count = 10;
```

Further, we declate application-specific data structures for our application:

```
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
```

In our application, when the server receives a message,  it sends back some response. The field `message_id` of the response should be equal to the same field of the client request, this helps the client to distinguish between server responses if multiple client requests have been sent.

The server code is below:

```
int server()
{
	std::cout<<"I'm a server."<<std::endl;
	std::cout<<"Use Ctrl^C to stop me."<<std::endl;
	boost::asio::io_context context;
	Server srv(context);
	srv.startAccept(port);

	size_t active_session_count = 0;
	//the main cycle
	while(true) // here could be any stop condition
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
	}
	std::cout<<"Server finished"<<std::endl;
	return 0; 
}
```
This is a server function that implements our simple business logic:
```
void serverProcessMessage(const ClientToServerMsg& input, ServerToClientMsg& output)
{
	output.message_id = input.message_id;
	output.content = input.content*2;
}
```
The client sends ten requests, waits for ten responses, prints them and exits:
```
int client()
{
	std::cout<<"I'm a client."<<std::endl;
	Client client;
	client.sync_connect("127.0.0.1", port);
	if(client.getStatus() != Client::Status::connectedOk)
	{
		std::cout<<"Can't connect to remote server (is server running?)"<<std::endl;
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
		    std::cout<<server_response.message_id<<":"<<server_response.content<<std::endl;
		    response_count++;
		}
		client.poll();
	}
	std::cout<<"Client finished"<<std::endl;
	return 0;
}
```
The full source code for this example your can find in the folder `example`

(C) Egor Tsvetkov, 2023-2025

