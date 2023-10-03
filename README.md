# Intro

## What is this? 

This is a yet another example of how to use C++ `boost::asio` library. In
original `boost::asio` examples, a communicator object (which represents a
connection and holds call-back functions) is blocked from deletion until the
asynchronous operation completes. In this example, we return control over the 
lifetime of such objects to the programmer.
Such behaviour can be more appropriate in some applications (see below).

## Why?

In the original `boost::asio` examples, the actual moment when communicator
object is deleted depends on when the last asynchronous operation is completed. 
In other words, it depends on the speed of the Internet and how fast the answer
will be sent by the other party. When the resources are limited (for example, 
if we develop some server application that should free resources as soon as 
possible), such behaviour is unacceptable.

The discussion of real-life examples can be found below.

# Technical details

## How boost::asio works

There are a lot of approaches how to use `boost::asio` demonstrated
on the [official site](https://www.boost.org/doc/libs/master/doc/html/boost_asio/examples.html).

The asio library supports synchonous and asynchronous calls. Here we discuss
asynchronous calls since they are non-blocking.

Asynchonous calls use `io_context` object containing the information about statuses
of each asynchronous operation. Main program should call `context.poll()` (or any similar function)
to proceed in such operations. When some user action is needed, `io_context.poll`
executes call-back functions provided by user. These function have read or write
access to the application buffers, and usually the buffer and the call-back function
are the members of the same object. Let us call such object a connection object.

When the connection object is destroyed but the connection is not closed gracefully,
the call to `io_context.poll` invoke the call-back function that is member of the
this deleted object. In simple programs (like examples in the documentation for
`boost::asio`) it is very easy to guarantee that `io_context::poll` will not be called
after the object have been destroyed.

In more complex applications, one `io_context` object can serve more than one connection, and
each call-back function should check if the parent object is still alive (or, at least
if the message buffer is not deleted yet).

## How to avoid


In the original `boost::asio` example,  call-back lambda functions capture
not only `this` pointer to the communicator object, but also an additional 
shared pointer to `this` object called `self`. Self shared pointer prevents 
deletion of the communicator object until the network operation is completed. 


But what program should do when the communicator object is deleted but 
asynchronious operation is not finished yet?
Our answer is the following: a call-back function should check whether object 
is still alive or already deleted.


# Solution 

So, we replaced `shared_ptr` to `weak_ptr` to prevent blocking of the 
communicator object from deletion. In additional, we added some code to check
if the parent communicator object is still alive. Finally, we added some code to
check different network error types.


# Example when it can be used

`boost::asio::io_context::poll` can call call-back function

1. When application has destroyed the communication object, it should not call `poll` function any more.
It is easy to implemented in simple applications (naturally, many simple applications implements this logic), 
but it is hard to implement if the application logic is very complex. 

2. For example, application can have multiple connections that use the same context object. And when
one of them has been destroyed, application continues to call `context::poll` to handle
with other connections.

Imagine we develop a client-server application. If user presses X button on the 
client's window, he expects that it will be closed immediately. He doesn't want to
wait until all network sessions will be finished. So, all objects should be deleted immediately. 
The exact order of deletion depends on the global structure of the application and we can't
change it. Also, calls of destructors can be mixed with other instructions,
including `boost::asio::io_context::poll`. This function can call out call-back function
after our communicator object is already destroyed.

Let us consider a server part. Let it be a server that talks with other services when preparing the
answer for the user request (for example, it could communicated with DB services). When user cancels his request, the server could immediately destroy
all objects that was created to communicate with other services. 

graceful close


timers are called by poll function


doReadHeader is always running because it is waiting for data
