#include "csapp/csapp.h"
#include "utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// You may assume that all requests sent to a node are less than this length
#define REQUESTLINELEN 128
#define HOSTNAME "localhost"

// Cache related constants
#define MAX_OBJECT_SIZE 512 // object here refers to the posting list or result list being cached
#define MAX_CACHE_SIZE MAX_OBJECT_SIZE*128

/* This struct contains all information needed for each node */
typedef struct node_info {
  int node_id;     // node number
  int port_number; // port number
  int listen_fd;   // file descriptor of socket the node is using
} node_info;

/* Variables that all nodes will share */

// Port number of the parent process. After each node has been spawned, it 
// attempts to connect to this port to send the parent the request for it's own 
// section of the database.
int PARENT_PORT = 0;

// Number of nodes that were created. Must be between 1 and 8 (inclusive).
int TOTAL_NODES = 0;

// A dynamically allocated array of TOTAL_NODES node_info structs.
// The parent process creates this and populates it's values so when it creates
// the nodes, they each know what port number the others are using.
node_info *NODES = NULL;

/* ------------  Variables specific to each child process / node ------------ */

// After forking each node (one child process) changes the value of this variable
// to their own node id.
// Note that node ids start at 0. If you're just implementing the single node 
// server this will be set to 0.
int NODE_ID = -1;

// Each node will fill this struct in with it's own portion of the database.
database partition = {NULL, 0, NULL};

/** @brief Called by a child process (node) when it wants to request its partition
 *         of the database from the parent process. This will be called ONCE by 
 *         each node in the "digest" phase.
 *  
 *  @todo  Implement this function. This function will need to:
 *         - Connect to the parent process. HINT: the port number to use is 
 *           stored as an int in PARENT_PORT.
 *         - Send a request line to the parent. The request needs to be a string
 *           of the form "<nodeid>\n" (the ID of the node followed by a newline) 
 *         - Read the response of the parent process. The response will start 
 *           with the size of the partition followed by a newline. After the 
 *           newline character, the next size bytes of the response will be this
 *           node's partition of the database.
 *         - Set the global partition variable. 
 */
void request_partition(void) {
  char port_num[8];
  rio_t rio;
  port_number_to_str(PARENT_PORT, port_num);
  char request[REQUESTLINELEN];
  snprintf(request, REQUESTLINELEN, "%i\n", NODE_ID);

  int clientfd = Open_clientfd(HOSTNAME, port_num);
  if (clientfd < 0) {
    fprintf(stderr, "Open_clientfd error: %i\n", errno);
    return;
  }

  char strpartition[MAXBUF];
  Rio_readinitb(&rio, clientfd);
  Rio_writen(clientfd, request, REQUESTLINELEN);
  Rio_readlineb(&rio, strpartition, REQUESTLINELEN);

  ssize_t partition_size;
  sscanf(strpartition, "%zd", &partition_size);
  partition.db_size = partition_size;
  partition.m_ptr = Malloc(partition_size);

  if (Rio_readnb(&rio, partition.m_ptr, partition.db_size) < 0) {
    fprintf(stderr, "Rio_readnb error: %i\n", errno);
    free(partition.m_ptr);
  } 
  else {
    build_hash_table(&partition);
  }

  Close(clientfd);
}

// void request_partition(void) {
//   char port_num[8];
//   rio_t rio;
//   port_number_to_str(PARENT_PORT, port_num);
//   char request[REQUESTLINELEN];
//   snprintf(request, REQUESTLINELEN, "%i\n", NODE_ID);

//   int clientfd = Open_clientfd(HOSTNAME, port_num);
//   if (clientfd < 0) {
//     fprintf(stderr, "Open_clientfd error: %i\n", errno);
//     return;
//   }

//   Rio_readinitb(&rio, clientfd);
//   Rio_writen(clientfd, request, REQUESTLINELEN);
//   Rio_readlineb(&rio, request, REQUESTLINELEN);

//   partition.db_size = atoi(request);
//   partition.m_ptr = Malloc(sizeof(char) * partition.db_size);

//   if (Rio_readnb(&rio, partition.m_ptr, partition.db_size) < 0) {
//     fprintf(stderr, "Rio_readnb error: %i\n", errno);
//     free(partition.m_ptr);
//   } 
//   else {
//     build_hash_table(&partition);
//   }

//   Close(clientfd);
// }



// Forwards a lookup request to a remote node
// Returns the value array of the query, and NULL otherwise
value_array* forward_request(char* query, int node_id) {
  rio_t rio;
  char port_num[8];
  char buf[MAXBUF];
  value_array *values = NULL;

  port_number_to_str(NODES[node_id].port_number, port_num);
  int remote_clientfd = Open_clientfd(HOSTNAME, port_num);

  Rio_readinitb(&rio, remote_clientfd);
  Rio_writen(remote_clientfd, query, REQUESTLINELEN);
  Rio_readlineb(&rio, buf, MAXBUF);

  Close(remote_clientfd);
  values = create_value_array(buf);

  return values;
}


// ADD COMMENT EVENTUALLY
void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);

  rio_t rio;
  Rio_readinitb(&rio, connfd);
  char query[REQUESTLINELEN]; // Client query

  while (Rio_readlineb(&rio, query, REQUESTLINELEN) != 0) {
    char message[MAXBUF]; // Message written to client
    request_line_to_key(query);

    if (strlen(query) == 0) {
      break;
    }
    // Single key lookup
    else if (strchr(query, ' ') == NULL) {
      int node = find_node(query, TOTAL_NODES);
      value_array *values;
      if (NODE_ID == node) {
        int index = lookup_find(partition.h_table, query);
        if (index != -1) {
          bucket bucket = partition.h_table->buckets[index];
          values = get_value_array(bucket.word);
          char strvalues[MAXBUF];
          value_array_to_str(values, strvalues, MAXBUF);
          sprintf(message, "%s%s", query, strvalues);
        }
        else {
          sprintf(message, "%s not found\n", query);
        }
      } 
      else {
        values = forward_request(query, node);
        if (values == NULL) {
          sprintf(message, "%s not found\n", query);
        } 
        else {
          char strvalues[MAXBUF];
          value_array_to_str(values, strvalues, MAXBUF);
          sprintf(message, "%s%s", query, strvalues);
        }
      }
    }
    // Intersection query
    else {
      char *key1 = strtok(query, " ");
      // Get second key by passing NULL into strtok
      char *key2 = strtok(NULL, " ");
      int node1 = find_node(key1, TOTAL_NODES);
      int node2 = find_node(key2, TOTAL_NODES);
      value_array *values1;
      value_array *values2;

      if (node1 == NODE_ID) {
        int index1 = lookup_find(partition.h_table, key1);
        if (index1 == -1) {
          values1 = NULL;
        }
        else {
          bucket bucket1 = partition.h_table->buckets[index1];
          values1 = get_value_array(bucket1.word);
        }
      }
      else {
        values1 = forward_request(key1, node1);
      }

      if (node2 == NODE_ID) {
        int index2 = lookup_find(partition.h_table, key2);
        if (index2 == -1) {
          values2 = NULL;
        }
        else {
          bucket bucket2 = partition.h_table->buckets[index2];
          values2 = get_value_array(bucket2.word);
        }
      } 
      else {
        values2 = forward_request(key2, node2);
      }

      if (values1 == NULL && values2 == NULL) {
        sprintf(message, "%s not found\n%s not found\n", key1, key2);
      }
      else if (values1 == NULL) {
        sprintf(message, "%s not found\n", key1);
      }
      else if (values2 == NULL) {
        sprintf(message, "%s not found\n", key2);
      }
      else {
        value_array *intersection = get_intersection(values1, values2);
        char values[MAXBUF];
        value_array_to_str(intersection, values, MAXBUF);
        sprintf(message, "%s,%s%s", key1, key2, values);
      }
    }
    Rio_writen(connfd, message, strlen(message));
  }
  Close(connfd);
  return NULL;
}

/** @brief The main server loop for a node. This will be called by a node after
 *         it has finished the digest phase. The server will run indefinitely,
 *         responding to requests. Each request is a single line. 
 *
 *  @note  The parent process creates the listening socket that the node should
 *         use to accept incoming connections. This file descriptor is stored in
 *         NODES[NODE_ID].listen_fd. 
*/
void node_serve(void) {
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  listenfd = NODES[NODE_ID].listen_fd;

  while (1) {
    clientlen = sizeof(struct sockaddr_storage);
    connfdp = Malloc(sizeof(int));                              // line:conc:echoservert:beginmalloc
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:conc:echoservert:endmalloc
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

/** @brief Called after a child process is forked. Initialises all information
 *         needed by an individual node. It then calls request_partition to get
 *         the database partition that belongs to this node (the digest phase). 
 *         It then calls node_serve to begin responding to requests (the serve
 *         phase). Since the server is designed to run forever (unless forcibly 
 *         terminated) this function should not return.
 * 
 *  @param node_id Value between 0 and TOTAL_NODES-1 that represents which node
 *         number this is. The global NODE_ID variable will be set to this value
 */
void start_node(int node_id) {
  NODE_ID = node_id;

  // close all listen_fds except the one that this node should use.
  for (int n = 0; n < TOTAL_NODES; n++) {
    if (n != NODE_ID)
      Close(NODES[n].listen_fd);
  }

  request_partition();
  node_serve();
}



/** ----------------------- PARENT PROCESS FUNCTIONS ----------------------- **/

/* The functions below here are for the initial parent process to use (spawning
 * child processes, partitioning the database, etc). 
 * You do not need to modify this code.
*/


/** @brief  Tries to create a listening socket on the port that start_port 
 *          points to. If it cannot use that port, it will subsequently try
 *          increasing port numbers until it successfully creates a listening 
 *          socket, or it has run out of valid ports. The value at start_port is
 *          set to the port_number the listening socket was opened on. The file
 *          descriptor of the listening socket is returned.
 * 
 *  @param  start_port The value that start_port points to is used as the first 
 *          port to try. When the function returns, the value is updated to the
 *          port number that the listening socket can use. 
 *  @return The file descriptor of the listening socket that was created, or -1
 *          if no listening socket has been created.
*/
int get_listenfd(int *start_port) {
  char portstr[PORT_STRLEN]; 
  int port, connfd;
  for (port = *start_port; port < MAX_PORTNUM; port++) {
    port_number_to_str(port, portstr);
    connfd = open_listenfd(portstr);
    if (connfd != -1) { // found a port to use
      *start_port = port;
      return connfd;
    }
  }
  return -1;
}

/** @brief  Called by the parent to handle a single request from a node for its
 *          partition of the database. 
 *
 *  @param  db The database that will be partitioned. 
 *  @param  connfd The connected file descriptor to read the request (a node id) 
 *          from. The partition of the database is written back in response.
 *  @return If there is an error in the request returns -1. Otherwise returns 0.
*/
int parent_handle_request(database *db, int connfd) {
  char request[REQUESTLINELEN];
  char responseline[REQUESTLINELEN];
  char *response;
  int node_id;
  ssize_t rl;
  size_t partition_size = 0;
  if ((rl = read(connfd, request, REQUESTLINELEN)) < 0) {
    fprintf(stderr, "parent_handle_request read error: %s\n", strerror(errno));
    return -1;
  }
  sscanf(request, "%d", &node_id);
  if ((node_id < 0) || (node_id >= TOTAL_NODES)) {
    response = "Invalid Request.\n";
    partition_size = strlen(response);
  } else {
    response = get_partition(db, TOTAL_NODES, node_id, &partition_size);
  }
  snprintf(responseline, REQUESTLINELEN, "%lu\n", partition_size);
  rl = write(connfd, responseline, strlen(responseline));
  rl = write(connfd, response, partition_size);
  return 0;
}

/** Called by the parent process to load in the database, and wait for the child
 *  nodes it created to send a message requesting their portion of the database.
 *  After it has received the same number of requests as nodes, it unmaps the 
 *  database. 
 *
 *  @param db_path path to the database file being loaded in. It is assumed that
 *         the entries contained in this file are already sorted in alphabetical
 *         order.
 */
void parent_serve(char *db_path, int parent_connfd) {
  // The parent doesn't need to create/populate the hash table.
  database *db = load_database(db_path);
  struct sockaddr_storage clientaddr;
  socklen_t clientlen = sizeof(clientaddr);
  int connfd = 0;
  int requests = 0;

  while (requests < TOTAL_NODES) {
    connfd = accept(parent_connfd, (SA *)&clientaddr, &clientlen);
    parent_handle_request(db, connfd);
    Close(connfd);
    requests++;
  }
  // Parent has now finished it's job.
  Munmap(db->m_ptr, db->db_size);
}

/** @brief Called after the parent has finished sending each node its partition 
 *         of the database. The parent waits in a loop for any child processes 
 *         (nodes) to terminate, and prints to stderr information about why the 
 *         child process terminated. 
*/
void parent_end() {
  int stat_loc;
  pid_t pid;
  while (1) {
    pid = wait(&stat_loc);
    if (pid < 0 && (errno == ECHILD))
      break;
    else {
      if (WIFEXITED(stat_loc))
        fprintf(stderr, "Process %d terminated with exit status %d\n", pid, WEXITSTATUS(stat_loc));
      else if (WIFSIGNALED(stat_loc))
        fprintf(stderr, "Process %d terminated by signal %d\n", pid, WTERMSIG(stat_loc));
    }
  }
}

int main(int argc, char const *argv[]) {
  int start_port;    // port to begin search
  int parent_connfd; // parent listens here to handle distributing database 
  int n_connfd;      
  pid_t pid;
  
  if (argc != 4) {
    fprintf(stderr, "usage: %s [num_nodes] [starting_port] [name_of_file]\n", argv[0]);
    exit(1);
  }
  
  sscanf(argv[1], "%d", &TOTAL_NODES);
  sscanf(argv[2], "%d", &start_port);

  if (TOTAL_NODES < 1 || (TOTAL_NODES > 8)) {
    fprintf(stderr, "Invalid node number given.\n");
    exit(1);
  } else if ((start_port < 1024) || start_port >= (MAX_PORTNUM - TOTAL_NODES)) {
    fprintf(stderr, "Invalid starting port given.\n");
    exit(1);
  }

  NODES = calloc(TOTAL_NODES, sizeof(node_info));
  parent_connfd = get_listenfd(&start_port);
  PARENT_PORT = start_port;

  for (int n = 0; n < TOTAL_NODES; n++) {
    start_port++; // start search at previously assigned port + 1
    n_connfd = get_listenfd(&start_port);
    if (n_connfd < 0) {
      fprintf(stderr, "get_listenfd error\n");
      exit(1);
    }
    NODES[n].listen_fd = n_connfd;
    NODES[n].node_id = n;
    NODES[n].port_number = start_port;
  }

  // Begin forking all child processes.
  for (int n = 0; n < TOTAL_NODES; n++) {
    if ((pid = Fork()) == 0) { // child process
      Close(parent_connfd);
      start_node(n);
      exit(1);
    } else {
      node_info node = NODES[n];
      fprintf(stderr, "NODE %d [PID: %d] listening on port %d\n", n, pid, node.port_number);
    }
  }

  // Parent closes all fd's that belong to it's children
  for (int n = 0; n < TOTAL_NODES; n++)
    Close(NODES[n].listen_fd);

  // Parent can now begin waiting for children to send messages to contact.
  parent_serve((char *) argv[3], parent_connfd);
  Close(parent_connfd);

  parent_end();

  return 0;
}
