# Database Query Server

## Overview
The server implemented in this assignment aims to process client queries for key value requests. Upon initialisation of the server, the parent process forks a number of child nodes and sends each node their respective partition of the database which is used to serve the client. Due to multithreading capabilities of the server, several clients can connect to the same node simultaneously to request a lookup or intersection query.

Individual nodes in the server handle requests by processing user queries as tokens, allowing the node to identify the query type. This is determined by checking if the query contains a single word or two words separated by one space, in which case results in a lookup query or intersection query respectively.

For look up queries, the node (assuming single node) searches its respective hash table for the provided key and returns its values, otherwise it will return that the key was not found. A similar process is performed for intersection, such that the node will perform a lookup for both provided keys. If either one of the keys are not found, then the node will return the respective unfound messages. Otherwise, the node performs an intersection between the two value arrays and return the corresponding values to the client.

## Additional Features
#### Multiple nodes and Forwarding
Forwarding between nodes was implemented to handle scenarios where the client-requested key exists in the database but not within the current node’s partition. In such cases, the node serving the client will forward the request to a remote node whose partition contains the desired key.

When handling the query, find_node() is called on the respective key and checks if the returned NODEID matches the current NODEID. In the case where the NODEID returned from find_node() is not the same as the current node, forward_request() is called. As suggested by the function name, forward_request() forwards the client request by calling open_clientfd(), which connects to the remote node at the specific port. Since all initialised nodes are waiting for connections, sending a client connection will immediately establish a connection. The query is then processed within the remote node and returns the key followed by its values. Similarly, in the case the key cannot be found, the remote node will return ‘key not found’.

#### Multithreading
Basic multithreading within the server was implemented through spawning an arbitrary number of threads to handle new connection requests, allowing for concurrency within the server for each individual node. These individual threads operate within the node_serve() function, which calls Pthread_create() to create a threadpool, and further assigns a thread upon client connection.

When observing the multithreading process, the node waits for a new client connection and then calls sbuf_insert(), assigning one of the individual threads to the client request. This is an efficient process for multithreading, in that only an arbitrary number of threads need to be created (in this case, 8 threads).

## Design Decisions
forward_request() was implemented with the assumption that the request is a standard lookup query. This allowed forwarding requests for regular lookups to be extremely efficient by simply calling the function if the key was not found in the partition. This was not much different for intersection queries as forward_request() was required to be called twice; on both keys of the query, and then taking the intersection of the returned values. Overall, implementing a helper function which processes a lookup query from a remote node improved the quality of the code and heavily reduces the code smells in the program.

Multithreading was initially implemented before forwarding between nodes as it would save a lot of programming time. It allowed for less refactoring in comparison to first implementing forwarding between multiple nodes. Hence, reducing the probability of breaking the program’s functionality and introducing new code smells after implementation. The echoservert.c file for lab 10 was used as the initial skeleton of the multithreaded server, with node_serve() acting as the main function in the file, creating threads upon new user connections to nodes. After implementing forwarding, multithreading was improved with reference to CMU's echoservert_pre.c (referenced in SoO) whilst utilising the sbuf package. This implementation of threadpooling can be much more memory efficient than having to spawn a new thread to handle each individual request.

## Observations
When running the provided tests against my code after my initial implementation, I noticed that some of the tests were passing at random. This turned out to be caused by errors within my Rio_writen calls as “Rio_writen error: bad file descriptor” was observed several times when inspecting the server output. This issue was resolved by reducing the number of Rio_writen calls, as consecutive Rio_writen calls were made within the code. This fix prevented additional calls from breaking the connfd utilised by threads, allowing the server to consistently pass all tests without any Rio_writen error.

When observing the server output after processing a query, the query string is not emptied, but instead passes additional ASCII values as queries for processing. Although sounding harmful to the program, this did not seem to hinder the performance or produce any errors as it was handled accordingly before the query was processed.

## Conclusion
The server efficiently handles client requests across multiple nodes. Key features, including node-to-node forwarding and multithreading streamlines efficient query handling and usage of partitioned databases. The implementation of multithreading before forwarding and inclusion of the forward_request() function significantly reduces the number of code smells, minimising potential errors when processing requests. Overall, the architecture of this server demonstrates a well-structured database management for handling client requests in a server-client environment.
