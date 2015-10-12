/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina
 *
 *  A simple web proxy in the C++ programming language under Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

#include <thread>
#include <string>
#include <queue>
#include <mutex>

// number of clients that can be in the backlog
#define MAX_BACKLOG 10

// size of the buffer for packets
#define PACK_SIZ 2048

// Number of threads that will be processing the event queue
#define WORKER_THREADS 1

//Default port number
#define DEFAULT_PORT 8000

// 0 = No Debugging, 1 = Some, 2 = Full
#define DEBUG 1

// Connection status constants
#define COMPLETE -1
#define UNINITIALIZED 0
#define MAILING 1
#define FAILED 2

struct ProxyConnection{
  int clientSock;
  int serverSock;
  int status;
  const char *host;
};

std::queue<ProxyConnection*> event_queue;
std::mutex event_lock;

// Boolean to say if threads should be running
bool serving = true;

int num_cxns = 0;

/*
 * Prints a line of ----'s
 */
void print_break(){
  printf("---------------------------------------------------------\n");
}

/*
 * Defines the process of closing a connection
 */
void free_connection(ProxyConnection* conn)
{
  if (DEBUG >= 1) printf("Terminating cxn with %s\n", conn->host);

  shutdown(conn->clientSock, 2);
  shutdown(conn->serverSock, 2);

  free(conn);
  
  num_cxns--;
}

/*
 * Defines a graceful pattern of shutdown for all current events;
 */
void graceful_end(int signum){
  // Tell threads to stop running
  serving = false;

  printf("\nEnding threads in: 3...\n"); sleep(1);
  printf("Ending threads in: 2...\n"); sleep(1);
  printf("Ending threads in: 1...\n"); sleep(1);

  event_lock.lock();
  while(!event_queue.empty()){
    ProxyConnection* conn = event_queue.front();
    event_queue.pop();
    free_connection(conn);
  }

  exit(signum);
}

/*
 * Blocks until an event is available
 */
ProxyConnection* get_event()
{
  ProxyConnection* next_event;

  while (true) {
    while (event_queue.empty());

    event_lock.lock();

    if (!event_queue.empty()) {
      next_event = event_queue.front();
      event_queue.pop();

      event_lock.unlock();

      break;
    }
    event_lock.unlock();
  }

  return next_event;
}

/*
 * Uses a lock to enqueue connections in a secure way.
 */
void enqueue_connection(ProxyConnection* c)
{
  event_lock.lock();
  event_queue.push(c);
  event_lock.unlock();
}

/*
 * Enqueue incoming connections for processing
 */
void serve(int listen_sock, struct sockaddr_in my_addr)
{
  while (true) {
    int new_cli_sock = 0;
    int addr_len = sizeof(my_addr);

    // When we get a new connection, we enqueue it
    if ((new_cli_sock = accept(listen_sock,
			       (struct sockaddr *)&my_addr,
			       (socklen_t *)&addr_len)))
      {
	ProxyConnection* new_conn;
	new_conn = (ProxyConnection*) malloc(sizeof(ProxyConnection));

	new_conn->clientSock = new_cli_sock;
	new_conn->status = UNINITIALIZED;

	enqueue_connection(new_conn);
    }
  }
}

/*
 * The main loop which listens on the designated port and creates the
 * connection structs
 */
void listen_and_serve(int port)
{
  struct sockaddr_in my_addr;
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = INADDR_ANY;

  // Create the socket
  int listen_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  // Bind socket and exit if failed.
  if (bind(listen_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
    perror("Self failed to bind.\n");
    graceful_end(1);
  }

  // Listen on the socket
  listen(listen_sock, MAX_BACKLOG);

  // Handle creation of sockets for incoming connections
  serve(listen_sock, my_addr);
}

/*
 * The first step in processing a connection.
 * Tears out the host path from the http body.
 * Gathers the optional port.
 * Returns whether the connection was set up successfully
 */
bool init_connection(ProxyConnection* conn)
{
  if (DEBUG >= 1) printf("Initializing connection\n");
  num_cxns++;

  char buf[PACK_SIZ];
  int len;

  if ((len = recv(conn->clientSock, buf, sizeof(buf), 0))){

    std::string send_buf = "";
    std::string host = "";
    std::string port = "";
    size_t i = 0;

    // Copy command (GET, etc)
    do {
      send_buf.push_back(buf[i]);
    } while (buf[i++] != ' ');

    // Skip http or https and the "://"
    while (buf[i++] != ':');
    i+=2;

    // Copy host name (goes up to a : or /)
    while (buf[i] != '/' && buf[i] != ':') {
      host.push_back(buf[i++]);
    }

    // If a port is designated, listen on that port.
    if (buf[i] == ':') {
      i++;
      while(buf[i] != '/') port.push_back(buf[i++]);
    }
    
    // Copy rest of address
    do {
      send_buf.push_back(buf[i]);
    } while (buf[i++] != ' ');

    // Skip HTTP/1.1
    while (buf[i++] != '\n');

    // Add HTTP/1.0
    send_buf += std::string("HTTP/1.0\n");

    // Finish copy (copy rest)
    send_buf += std::string(&buf[i]);

    if (send_buf.find("Connection: keep-alive") != std::string::npos){
      send_buf.replace(send_buf.find("Connection: keep-alive"), 22,
		       "Connection: close");
    }

    struct hostent *hp = gethostbyname(host.c_str());
    if (!hp) {
      printf("unknown host: %s\n, closing connection", host.c_str());
      return false;
    }
    conn->host = host.c_str();

    int server_port = port.empty() ? 80 : atoi(port.c_str());

    // Build address structure
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    bcopy(hp->h_addr, (char*)&sin.sin_addr, hp->h_length);
    sin.sin_port = htons(server_port);

    // Open a socket connection
    if ((conn->serverSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      printf("error in socket to destination\n");
      return false;
    }

    if (DEBUG > 1) {
      // Print the edited buffer
      print_break();
      fputs(buf, stdout);
      print_break();
      fputs(send_buf.c_str(), stdout);
      printf("\n");
    }

    // Attempt to connect to destination server.
    if (connect(conn->serverSock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
      printf("couldn't connect to the destination socket\n");
      return false;
    }

    int content_len = strlen(send_buf.c_str()) + 1;
    if (send(conn->serverSock, send_buf.c_str(), content_len, 0) < 0) {
      printf("Error in initial send\n");
      return false;
    }
  }

  if (DEBUG >= 1) printf("Connection with %s established\n", conn->host);

  return true;
}

/*
 * Forwards PACK_SIZ bytes from src_sock to dest_sock
 * Returns number of bytes forwarded
 */
int forward(int src_sock, int dest_sock)
{
  char buf[PACK_SIZ];
  int len = recv(src_sock, buf, sizeof(buf), MSG_DONTWAIT);

  if (len > 0) {
    send(dest_sock, buf, len, 0);
  }

  return len;
}

/*
 * Forward packets between each end of a connection
 * return whether or not there are any remaining packets to send
 */
bool exchange_packets(ProxyConnection* conn) 
{
  if (DEBUG > 1) printf("Exchanging packets\n");

  int sent_to_server = forward(conn->clientSock, conn->serverSock);
  int sent_to_client = forward(conn->serverSock, conn->clientSock);
  
  if (DEBUG >= 1 && (sent_to_server > 0 || sent_to_client > 0)) {
    printf("Client: %i <--> %i :Server\n", sent_to_server, sent_to_client);
  }
  
  return sent_to_server || sent_to_client;
}

/*
 * The process run by each provided thread.
 * Grabs the top event on the queue and takes does one action on it,
 *  depending on the state.
 */
void *process_queue()
{
  while (serving) {
    ProxyConnection* conn = get_event();

    if (conn->status == UNINITIALIZED) {
      conn->status = init_connection(conn) ? MAILING : FAILED;
      if (DEBUG >= 1) printf("Num cxns: %d\n", num_cxns);
    } 
    else if (conn->status == MAILING && !exchange_packets(conn)) {
      conn->status = COMPLETE;
    }

    if (conn->status == COMPLETE || conn->status == FAILED) {
      free_connection(conn);
      if (DEBUG >= 1) printf("Num cxns: %d\n", num_cxns);
    }
    else {
      // Throw it to the end of the queue
      if (DEBUG > 1) printf("Re-enqueing\n");
      enqueue_connection(conn);
    }
  }
}


/*
 * Spawns all threads to process the queue.
 */
void spawn_event_processors()
{
  std::thread threads[WORKER_THREADS];

  for (int i = 0; i < WORKER_THREADS; i++) {
    if (DEBUG > 1) printf("Thread %i Started\n", i);
    threads[i] = std::thread(process_queue);
  }

  print_break();

  for (int i = 0; i < WORKER_THREADS; i++) {
    threads[i].detach();
  }
}

int main(int argc, char** argv)
{
  // Sets the default behavior of failure states to close all connections.
  signal(SIGINT,  graceful_end);
  signal(SIGABRT, graceful_end);

  // Define port using commandline, if given
  int port = DEFAULT_PORT;

  if (argc > 1){
    int user_port = atoi(argv[1]);

    if (user_port > 1024) {
      port = user_port;
    }
    else {
      printf("Port must be > 1024\n");
      graceful_end(1);
    }
  }

  print_break();
  printf("Proxy server port: %i\n", port);
  print_break();

  // Threads to process events
  spawn_event_processors();

  // Listen for incoming (client) connections
  listen_and_serve(port);
}
