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

# Design

## Low-level message data type

Message consists of two parts: the header and the body. The size of header is fixed and known. It has only two fields: the unique signature of the header (which is hardcoded in the source files) and the size of the body in bytes. The body datatype is the alias for std::vector<uint8_t>.

The user is resposible to convert low-level messages to application specific high-level data types. For POD type the simplest way to do this is just using of memcpy or even reinterpret_cast. For more complex data types the boost::serialization can be recommended [boost::serialization](https://www.boost.org/doc/libs/1_86_0/libs/serialization/doc/tutorial.html).

message.hpp:
MsgHeader
Body


## Low-level Communication objects

The boost::asio library supports synchonous and asynchronous calls. In this project we choose asynchronous calls.

When we use asynchronous calls for i/o operations, we provide them buffers and callback functions. We should guarantee that these buffers will not be deleted until asynchronous operations are in progress. If callback function is a non-static member function we should also guarantee that the object containing the callback function will not be destroyed too.

Following encapsulation principle (and boost::asio philosophy), all buffers and callback functions should be included to the corresponding classes. Such classes provide object-oriented interface to low-level boost operations like `async_read` and `async_write`. Let us call the objects that contains all resources required for calling asynchronous operations the communication objects.

In boost::asio examples, the special mechanism prevents deletion of such objects when at least one asyncronous operation is in progress. This mechanism is based on creating shared_ptr to such objects and capturing it in lambda callback function. After callback function have been executed and destroyed, the corresponding shared_ptr is destroyed making it possible to destroy the communication object. We follow this approach to protect communication object from deletion during asynchronous operations. See XXX below

Since the shared pointer to the communication object is deleted only when asynchronous operation is finished or canceled, the life time of the communication objects is unpredictable. For this reason the communication objects should be light-weight objects and should contain only those resources that is necessary for asynchronous operation to complete.

The communication classes are Reader, Writer, Acceptor and Connector. Each communication class wraps only one asynchronous function.

asyncoperations.hpp: // rename to comobjects
AsyncOperationBase
Reader
Writer
Acceptor
Connector


## Mid-level Talker object (ASIOBufferedTalker)

The communication objects are responsible for single IO-operation, that is currently in progress. The queue of all messages that were received or planned to send is stored in the mid-level Talker object.

The talker object has two queues: one for ingoing messages and one for outgoing. It exposes method to read messages from the first queue and write messages to the second queue. Other ends of these queues are connected to the Reader and Writer objects. The operation of them are controlled by the Talker object itself.

ASIOBufferedTalker can be in one of the following states: notStarted, working, closed, failed. The state can be checked using isFinished, isWorking or getStatus member functions.


## Mid-level ASIOClient and ASIOServer

ASIOClient class can resolve the network address and connect to the specified server. It contains ASIOBufferedTalker object that is initialized if the connection is successfully established. After this ASIOClient becomes ready to send and receive messages.

ASIOServer class listens the specified port and accepts incoming connections. For each new connection ASIOServer creates a new ASIOBufferedTalker object and assigns to it the unique session id. The server exposes only the session id to identify the talker object.

ASIOServer provides methods for tracking new connections, and sending and receiving messages. Reading and writing operations require the session id as parameter. In addition, the ASIOServer can read the message from the first session which contains non-empty queue of ingoing messages.


ASIOClient can have one of the followng statuses:  notinitialized, resolving, connecting, connectedOk, disconnected, resolvingFailed, connectingFailed. The status of the client can be obtained using getStatus member function.


(Not implemented yet) ASIOServer can have one of the following statuses: notStarted, Listening, notListening, closed, terminated. The status of the server can be obtained using getStatus() member function


Since ASIOClient and ASIOServer classes contains callback functions they are derived from isAlive base class.


## Implementation details

Since we use shared pointers to prevent deletion of communication objects, we should construct such objects by calling `std::make_shared` instead of calling constructors directly. (WE use create_shared_from_this, and this is a reason why constructors should be private) So, we make constructors of such objects private and expose `create` static member function which returns `shared_ptr` to a newly created object. 


## High-level Client and Server objects

The high-level server and client objects wrap mid-level ASIOClient and ASIOServer objects respectively but unlike them the high-level objects can be created without using of the shared pointers.

(Not implemented yet) In addition, high-level objects support move semantics.



## Server and Client classes

What communication object do when it receives the message? It should notify the consumer of this message. Consumer is the object.

When consumer is already destroyed the message will be lost (or you can implement any other logic what to do in this case). Usually consumer is destroyed when we close the application.

- Message queue placed to ASIOBufferedTalker object which can be released immediately

- When asynchronous operation finishes, the callback member function is called. They should swap buffers. For example, in case of reading operation, the recently read buffer should be transfered to the parent object (in our case this is ASIOBufferedTalker, which holds buffers of incoming and outgoing messages), and a fresh buffer should be obtained for the next portion of data. Since the parent object can be already destroyed, this function should check it before calling any function of it. This can be achieved using `weak_ptr` to parent object. But we use a special mechanism called SafeCallback (see below) (why not weak_ptr?) weak_ptr requires that the object is created using make_shared, but we want to use objects without shared pointers (see below)

- Server and client objects can be created in any way you want. They can be globals, locals in function `main` (allocated on the stack),  or allocated in the heap. No matter how they are created and used, the asynchronous calls will work fine. This is the result of  using `isAlive` tracker.

- Finally, we generalize Server and Client classes by allowing them to send and receive any data type. This is achieved by using templates. Of course, when you do `read<YourMessageType>(msg)`, you must be sure that the network counterpartner sends exactly the same data  (with the same representation of this data in memory) (remove this? Replace with templated read/write?)

asiocomminicator.hpp:
Server
Client

## Safe callback implementation

When callback function is member function of some object, we should check if this object is still alive before calling this function. For this purpose we utilize a shared_ptr to bool variable. Our implementation consists of two classes. 

The first class `SafeCallback` is a wrapper for callback member function that is very close to std::function, but before invoking wrapped function it checks the boolean variable protecting this callback. 

The second class `IsAliveTracker` contains the shared pointer to the bool variable that has initial value set to true. When the destructor of this class is called, this bool variable is set to false indicating that the object is destroyed. This class is used as a base class for classes that wraps asynchronous operations.


safecallback.hpp:
SafeMemberFcnCallback





## Three types of networking apps

MAYBE MOVE TO ANOTHER PLACE
All networking applications can be divided into three main types depending on their usage scenarios.
Scenarios of different types have different requirements to the behaviour of an application (especially, when network operation finishes with an error).

1. The first scenario includes embedded applications which have very simple logic. Usually, such applications are very small and they are running on a dedicated hardware. All messages have simple static format. In case of a network error it is sufficient to restart the application or the device. Many of the original boost::asio examples belong to this class (echo server, chat server, daytime server).

3. The second class is desktop applications. Such applications have more complex logic to deal with network errors. They should recover after errors when it is possible or help the user to diagnose the reason of the error.

4. The third class is server applications that aren't running on a dedicated hardware. Since other programs can be run on the same hardware, such programs should release memory resources as soon as possible. 

This code is developed with the second and third categories in mind.


## How boost::asio works

Asynchronous calls are implemented in `boost::asio::io_context` object which contains information about current progress of each of the asynchronous operations committed to this object. Main program should call `io_context.poll()` (or any similar function) to proceed in such operations. When some asynchronous operation have been finished, `io_context.poll` invokes call-back functions provided by the user for this operation.  These functions consume (swap buffers) recently accepted data and/or initiate next asynchronous operation. 

THIS IS A MOTIVATION:
When the communication object (see above) is destroyed but the connection is not closed gracefully, `io_context.poll` can invoke the callback function that is a member of that deleted object. In simple programs (like examples in the documentation for `boost::asio`) it is very easy to ensure that `io_context::poll` will not be called after that object is destroyed (Programmer can easily check that `io_context::poll` is not called after destuction of buffers).

In more complex applications, one `io_context` object can serve more than one connection. In this case, `io_context::poll` may be invoked to serve one connection object, while another object has already been destroyed.

There are two solutions of this problem:
1. stop all asynchronous operations for destroyed connection objects (boost::asio provides cancellation slots,  or you can call [cancel function](https://www.boost.org/doc/libs/1_83_0/doc/html/boost_asio/reference/basic_stream_socket/cancel/overload2.html) (but it has some issues with portatibility).
2. each callback function should check if the corresponding connection object is still alive (in simple case, if the message buffer is not deleted yet). 

This example follows the second approach.



(C) Egor Tsvetkov, 2023

