Project:
Remote Procedure Call (RPC)

Goal:
To allow two indepdently running applications to communicate with each other.
1. One Application will be serving the RPC
2. Second Application will be client of the RPC

Features: 
1. Be able to define APIs using some simple IDL file - text file
2. Python  or (suggest some language for easy) Generator to generate both server and client side C++ and python code that applications can include and build their logic around it.
3. The OS should be able to support this feature of rpc between them - I suggest we use linux shared memory
4. Client will be able to call get/set api calls to server and get success/failure codes along with requested data.
5. Server should be able to "notify" clients of notifications apis if defined in the IDL file.

APIs:
0. Support data types - POD, arrays, custom structs, arrays of custom structs, cpp containers (may be ?)
1. Getters & Setters with above data types
2. Notifiers - apis that the server will notify the client asynchronously - will have parameters with above data types


See Requirements.md