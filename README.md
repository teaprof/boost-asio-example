# Intro

## What is this? 

This is a yet another example of how to use C++ `boost::asio` library. This code
can be used as a starting point to build you own project based on boost::asio.

Original examples of using boost::asio library can be found at
[official site](https://www.boost.org/doc/libs/master/doc/html/boost_asio/examples.html).


## What is boost::asio?

[boost::asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio.html) 
(acronym for ASynchronous Input/Output) is a famous C++ library which contains
a well-designed set of classes and free functions supporting network operations.
This library incorporates different approaches such as callback function and coroutines,
but you can easily switch between them.

Documentation on boost::asio can be found [here](https://beta.boost.org/doc/libs/develop/doc/html/boost_asio.html).


## Motivation

Original boost::asio examples and tutorial codes are very concise. This is good
for learning boost::asio library and understanding how it works, but real-life code 
is more complex than those examples and usually it requires more precise control of 
buffers lifetimes, different hooks placed through the whole code, tracking of
the connection status and many other features. Every time when I started a new project, 
I took one of the boost::asio examples as a starting point and addded the features
described above. After some such tries, I decided to prepare my own
example (the present code) of boost::asio wrapper and use it as a starting point in
my new projects.

## Three types of networking apps

All networking applications can be divided into three main types depending on their usage scenarios.
Scenarios of different types have different requirements to the behaviour of an application
(especially, when network operation finishes with an error).

1. The first scenario includes embedded applications which have very simple logic. 
Usually, such applications are very small and they are running on a dedicated hardware. All
messages have simple static format. In case of a network error it is sufficient to restart 
the application or the device. Many of the original boost::asio examples belong to this class
(echo server, chat server, daytime server).

3. The second class is desktop applications. Such applications have more complex
behaviour to deal with network errors. They should help user to diagnose the reason
of error, and some of them can recover after errors.

4. The third class is server applications that aren't running on a dedicated hardware.
Since other programs can be run on the same hardware, such programs should release
memory resources as soon as possible. 

This code is developed with the third category in mind.

# Design of a resource-friendly networking application

## Where is the problem?

The boost::asio library supports synchonous and asynchronous calls. Here we discuss
asynchronous calls since they should be preferred in high-performance applications.

When we use asynchronous calls of i/o operations, we provide them buffers and
we should guarantee that these buffers will not be deleted until these operations
are in progress. 

Following incapsulation principle (and boost::asio), we can declare a class which holds the i/o buffer
and callback member function which should be invoked when asynchronous operation
completes (so-called completion handler). This class also exposes a function which
starts asynchronous operation. Roughly speaking, such class is the wrapper for
asynchronous operation. Let us call such kind of objects the **communication objects**.

When the asynchronous operation is in progress, the communication object should not
be deleted. 

In boost::asio examples, the special mechanism prevents deletion of such objects 
when at least one asyncronous operation is in progress. This mechanism is based
on creating  shared_ptr to such object and capturing it in lambda callback function.
After callback function have been executed, the corresponding shared_ptr is destroyed
making possible to destroy the communication object.

When the program no longer needs the communication object, it deletes shared_ptr 
to this object. But the actual moment when communicator object will be 
deleted depends on when the last asynchronous operation is completed.
**In other words, the actual moment when the resources will be released depends
on the speed of the Internet and how quickly the response will be sent by the partner**.
Furthermore, if the destination host in
down, the application waits for some secs before an async operation completes with a non-zero
error code. 
When the resources are limited (for example, 
if we develop some server application that should free resources as soon as 
possible), such behaviour is unacceptable. 

Server application should destroy buffers of incoming and outgoing messages as soon as possible
since this buffers can consume large amount of RAM.


## How boost::asio works

Asynchonous calls are implemented in `boost::asio::io_context` object which contains information about current
progress of each of the asynchronous operations commited to this object. Main program should call `io_context.poll()` (or any similar function)
to proceed in such operations. When some asynchronous operation have been finished, `io_context.poll`
invokes call-back functions provided by the user for this operation. 
These functions consume recently accepted data and/or
initiate next asynchronous operation. 

When the communication object (see above) is destroyed but the connection is not closed gracefully,
calling to `io_context.poll` can invoke the callback function that is a member of that deleted object.
In simple programs (like examples in the documentation for
`boost::asio`) it is very easy to ensure that `io_context::poll` will not be called
after that object is destroyed.

In more complex applications, one `io_context` object can serve more than one connection. In this case,
`io_context::poll` may be invoked to serve one connection object, while another object has already been destroyed.

There are two solutions of this problem:
1. selectively stop all asynchronous operations for destroyed connection objects (boost::asio provides cancellation slots, 
or you can call [cancel function](https://www.boost.org/doc/libs/1_83_0/doc/html/boost_asio/reference/basic_stream_socket/cancel/overload2.html)
(but it has some issues with portatibility).
2. each callback function should check if the corresponding connection object is still alive (in simple case,
if the message buffer is not deleted yet). 

This example follows the second approach.

## Solution

The following ideas are implemented in the present code.

- Since we use shared pointers to prevent deletion of communication objects, we should construct such objects by calling `std::make_shared` instead of calling
constructors directly. So, we make constructors of such objects private and expose `create` static member function which
returns `shared_ptr` to a newly created object. 

- All communicator objects are lightweight objects, they contain only buffers they currently need for running asynchoronous
operations. Each communicator object wraps only one asynchonous function. This allows us to control their lifetimes more precisely,
since we can release these objects independently.

- Message queue placed to ASIOBufferedTalker object which can be released immediately (it doesn't have any protection mechanism 
to prevent deletion since it doesn't hold any resources needed for asynchronous operations).

- When asynchronous operation finishes, the callback member function is called. They should swap buffers. For example, 
in case of reading operation, the recently read buffer should be transfered to the parent object (in our case, it is
ASIOBufferedTalker, which holds
buffers of incoming and outgoing messages),
and a fresh buffer should be obtained for the next portion of data. Since the parent object can be already destroyed,
this function should check it before calling any function of it. This can be achieved using `weak_ptr` to parent object.
But we use a special mechanism called SafeCallback (see below).

- Server and client objects can be created in any way you want. They can be global variable, local variables of function `main` (placed to the stack), 
or allocated in the heap. No matter how they are created and used, the asynchronous calls will work fine. This is the result of 
using safe callback mechanism.


- Finally, we generalize Server and Client classes by allowing them to send and receive any data type. This is achieved
by using templates. Of course, when you do `read<YourMessageType>(msg)`, you should be sure that counterpartner sends exactly the same data 
(with the same representation of this data in memory).


## Safe callback implementation

When callback function is member function of some object, we should check if this object is still alive before calling this function. For 
this purpose we utilize a shared_ptr to bool variable. Our implementation consists of two classes. 

The first class `SafeCallback` is a wrapper for callback member function that is very close to std::function, but before invoking wrapped function it checks
the boolean variable protecting this callback.

The second class `CallbackProtector` is a sentinel class that is designed to be placed inside the class containing the callback member function. When the sentinel 
object is deleted, its destructor sets the boolean variable to false. This is a way how to protect member function from being called after the object is destroyed.

(C) Egor Tsvetkov, 2023
