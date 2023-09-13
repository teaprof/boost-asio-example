This is a yet another example of how to use C++ boost::asio library. In original boost::asio
examples object representing connection is blocked from deletion until asynchronious operation
completes. In this example, we return control over life time of such objects to the programmer.

In original boost::asio example each of call-back lambda functions captures this-pointer
and additional shared pointer to this object called `self`. Self shared pointer saves this
object from a deletion until network operation is completed. For some reasons, this
behaviour can be unacceptable under certain conditions (when you want
that `delete obj` should actually destroy object immediately without waiting).

But what program should do when object representing connection should be deleted but
asynchronious operation is not finished yet?
The answer accepted in this example is the following: object should be actually deleted
immediately, but a call-back function should check whether object is still alive or already deleted.

So, we replace shared_ptr to weak_ptr to avoid blocking of the object deletion and
return control of the object life-time to the programmer. In additional, we add some
code to check different error codes.

