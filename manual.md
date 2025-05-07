# Design

## Message data type

Message consists of two parts: the header represented by `MsgHeader` class and the body represented by `MsgBody` class. The size of header is fixed and known. It has only two fields: the unique signature of the header (which is hardcoded in the source files) and the size of the body in bytes. The body datatype is the alias for std::vector<uint8_t>.

Classes `MsgHeader` and `MsgBody` are declared in file `message.hpp`

The conversion of the message body from the low-level byte buffer to the application-specific data types can be made in the one of the following ways:

* For POD type the simplest way to do this is just using of memcpy or even reinterpret_cast.
* For more complex data types the boost::serialization can be recommended [boost::serialization](https://www.boost.org/doc/libs/1_86_0/libs/serialization/doc/tutorial.html).

## Low-level Communication objects

### Basics

The `boost::asio` library supports synchonous and asynchronous calls. In this project we use asynchronous calls.

The asychronous calls require that the preallocated buffer and callback functions are passed as parameters. We should guarantee that these buffers will not be deleted until asynchronous operations are in progress. If callback function is a non-static member function we should also guarantee that the object containing the callback function will not be destroyed too.

Let us call the objects that contains all resources required for calling asynchronous operations the communication objects. These resources include all buffers and callback member functions. Such objects provide object-oriented interface to low-level boost operations like `async_read` and `async_write`. 

The communication classes are `Reader`, `Writer`, `Acceptor` and `Connector`. Each communication class wraps only one asynchronous function. All of them are declared in the file `asyncoperations.hpp`. 
All communication classes are derived from `AsyncOperationBase`. This class hides the constructor but exposes the `create_shared_ptr` to create the shared pointer to a new object. For safety reasons, `AsyncOperationBase` deletes copy and move constructors (the objects can’t be moved while asynchronous operation is in progress).

Since the shared pointer to the communication object is deleted only when asynchronous operation is finished or canceled, the life time of the communication objects is unpredictable. For this reason the communication objects should be light-weight objects and should contain only those resources that is necessary for asynchronous operation to complete.


### Implementation details

Following the `boost::asio` examples, to prevent the deletion of the communication object when at least one asyncronous operation is in progress we use shared pointers. When the object calls asynchronous operation it creates lambda as a callback function that captures the shared pointer to `this`. After callback function have been executed and destroyed, the corresponding shared_ptr is destroyed making it possible to destroy the communication object. We follow this approach to protect communication object from deletion during asynchronous operations. See XXX below

Since we use shared pointers to prevent deletion of the communication objects, we should construct such objects by calling `std::make_shared` instead of calling their constructors directly. So, `AsyncOperationBase` class make constructors of such objects private and expose `create_shared_ptr` (and `create_shared_ptr_fast`) static member function that returns shared pointer to a newly created object. 


## Mid-level Talker object (ASIOBufferedTalker)

The communication objects are responsible for single IO-operations that are currently in progress. The queue of all messages that were received or planned to be sent is stored in the mid-level `ASIOBufferedTalker` object. This class is declared in `asioserverclient.hpp`

The talker object has two queues: one for ingoing messages and one for outgoing. It exposes methods to read messages from the first queue and to write messages to the second queue. Other ends of these queues are connected to the `Reader` and `Writer` objects discussed before. The operation of them are controlled by the talker object itself.

`ASIOBufferedTalker` can be in one of the following states: `notStarted`, `working`, `closed`, `failed`. The state can be checked using isFinished, isWorking or getStatus member functions.

`ASIOBufferedTalker` also deletes its copy and move constructors. The reason for this is that it provides its member functions to underlaying `Reader` and `Writer` objects that should be called on completion of the io operation. 

## Mid-level ASIOClient and ASIOServer

`ASIOClient` class can resolve the network address and connect to the specified server. It contains `ASIOBufferedTalker` object that is initialized if the connection is successfully established. After this, the `ASIOClient` object becomes ready to send and receive messages.

`ASIOServer` class listens the specified port and accepts incoming connections. For each new connection `ASIOServer` creates a new `ASIOBufferedTalker` object and assigns to it the unique `session_id`. The server exposes only the `session_id` to identify the talker object.

`ASIOServer` provides methods for tracking new connections, and sending and receiving messages. Reading and writing operations require the `session_id` as parameter. In addition, the `ASIOServer`  object can read the message from the first session which contains non-empty queue of ingoing messages.

Since `ASIOClient` and `ASIOServer` classes contains callback functions they are derived from isAlive base class (see [below](#safe-callback-implementation)).


`ASIOClient` can have one of the followng statuses:  `notinitialized`, `resolving`, `connecting`, `connectedOk`, `disconnected`, `resolvingFailed`, `connectingFailed`. The status of the client can be obtained using getStatus member function.

(*Not implemented yet*) `ASIOServer` can have one of the following statuses: notStarted, Listening, notListening, closed, terminated. The status of the server can be obtained using `getStatus()` member function

Both `ASIOClient` and `ASIOServer` classes are declared in `asioserverclient.hpp`.

## Safe callback implementation


When callback function is member function we should check if it's parent object is still alive before calling this function. For this purpose we utilize a `shared_ptr<bool>` variable named `is_alive`. The implementation consists of two classes. 

The first class `SafeMemberFcnCallback` is a smart wrapper for other function. It looks like std::function, but before invoking wrapped function it checks if its parent object is still alive.

The second class `IsAliveTracker` contains the shared pointer to the bool variable that has initial value set to true. When the destructor of this class is called, this bool variable is set to false indicating that the object is destroyed. This class is used as a base class for classes that wraps asynchronous operations.

Both classes `SafeMemberFcnCallback` and `IsAliveTracker` are declared in `safecallback.hpp`



## High-level Client and Server objects

### Basics

The high-level classes `Server` and `Client` objects wrap mid-level `ASIOClient` and `ASIOServer` objects respectively but unlike them the high-level objects can be created without using of the shared pointers.

The `Server` and `Client` classes are declared in `asioserverclient.hpp`.


The `Server` and `Client` classes are generalized by allowing them to send and receive any data type. This is achieved by using templates. Of course, when you do `read<YourMessageType>(msg)`, you must be sure that the network counterpartner sends exactly the same data  (with the same representation of this data in memory).

(*Not implemented yet*) In addition, high-level objects support move semantics.
 
