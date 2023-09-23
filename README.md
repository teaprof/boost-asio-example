# What is this? 

This is a yet another example of how to use C++ `boost::asio` library. In original `boost::asio`
examples, a communicator object (which represents a connection) is blocked from deletion until the asynchronous operation
completes. In this example, we return control over the lifetime of such objects to the programmer.

# Why?

In the original `boost::asio` examples, the actual time when communicator object is deleted depends on when the
last asynchronous operation is completed. In other words, it depends on the speed of the Internet and how fast the answer
will be sent by the other party. When the resources are limited (for example, if we develop some server application that should free resources
as soon as possible), such behaviour is unacceptable. 

# Technical details

In the original `boost::asio` example,  call-back lambda functions capture not only `this` pointer to
the communicator object, but also an additional shared pointer to `this` object called `self`. Self 
shared pointer prevents deletion of the communicator object until the network operation is completed. 


But what program should do when the communicator object is deleted but
asynchronious operation is not finished yet?
Our answer is the following: a call-back function should check whether object is still alive or already deleted.

# Solution 

So, we replaced `shared_ptr` to `weak_ptr` to prevent blocking of the communicator object from deletion.
In additional, we added some code to check if the parent communicator object is still alive. Finally, we
added some code to check different network error types.

