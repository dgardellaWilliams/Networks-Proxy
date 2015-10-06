/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C++ programming language under Linux.
 */


/***********************************************************************
  So we were wrong with the recieves.
  The connection is not finished so that recv will not return 0.
   
  Instead we need to read the content length parameter of the http 
  request! 
  
  If a request contains a message-body and a Content-Length is not given, 
  the server SHOULD respond with 400 (bad request) if it cannot determine
  the length of the message, or with 411 (length required) if it wishes 
  to insist on receiving a valid Content-Length.
  
  Once we've hit the length of the message we are done!
 ***********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <thread>
#include <string>
#include <string.h>
#include <queue>
#include <mutex>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/fcntl.h>

// number of clients that can be in the backlog
#define MAX_BACKLOG 10

// size of the buffer for requests
#define REQ_SIZ 2048

// Number of threads that'll be doing stuff
#define WORKER_THREADS 1

#define DEFAULT_PORT 8000

#define DEBUG 2 // 0 = No Debugging, 1 = Some, 2 = Full

// Connection status constants
#define COMPLETE -1
#define UNINITIALIZED 0
#define READING_CLIENT 1
#define READING_SERVER 2

int SERVER_PORT = 80;

struct ProxyConnection{
  // likely doesnt need to be stored here because ocne the socket
  // is created, you no longer need to store it
  int clientSock;
  int serverSock;
  int destPort;
  char * destAddr;
  int command; 
  int status;
};

std::queue<ProxyConnection*> event_queue;
std::mutex event_lock;

// Prints a line of ----'s
void print_break();

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

void enqueue_connection(ProxyConnection* c)
{
  event_lock.lock();
  
  event_queue.push(c); 

  event_lock.unlock();
}

/*
 * Open new sockets for incoming connections, and enqueue them
 * for processing
 */
void serve(int listen_sock, struct sockaddr_in my_addr)
{
  while (true) {
    int new_cli_sock = 0;
    int addr_len = sizeof(my_addr);
    
    // When we get a new connection, we enqueue it
    if ((new_cli_sock = accept(listen_sock, 
			       (struct sockaddr *) &my_addr, 
			       (socklen_t *) &addr_len))) 
      {
	ProxyConnection* new_connect = (ProxyConnection*) malloc(sizeof(ProxyConnection));
	new_connect->clientSock = new_cli_sock;
	new_connect->status = UNINITIALIZED;
	
	enqueue_connection(new_connect);
    }
  }  
}

int listen_and_serve(int port)
{  
  struct sockaddr_in my_addr;
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = INADDR_ANY;

  // Create the socket
  int listen_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  // Bind socket and exit if failed.
  if (bind(listen_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
    printf("failed to bind.\n");
    exit(1);
  }

  // Listen on the socket
  listen(listen_sock, MAX_BACKLOG);

  // Handle creation of sockets for incoming connections
  serve(listen_sock, my_addr);
}

void init_connection(ProxyConnection* conn)
{
  char buf[BUFSIZ];
  int len;
  struct hostent *hp;
  struct sockaddr_in sin;
  int sock;
  

  if (len = recv(conn->clientSock,buf,sizeof(buf),0)){
    printf("The original length was: %i\n", len);
    
    std::string convient(buf);
    std::string host;

    size_t fa = convient.find("Host: ");
    host = convient.substr(fa+6, convient.find('\n',fa) - fa -7);
   
    convient = convient.substr(0,convient.find("http")) + 
      convient.substr(convient.find(host.c_str()) + host.length(), 
		      convient.find(" ",convient.find(host.c_str())) - convient.find(host.c_str()));
    printf("Test string: %s\n",convient.c_str());
    exit(1);

    
    char sendBuf[BUFSIZ];
    
    
    //translate the host name into IP address
    hp = gethostbyname(host.c_str());
    if (!hp){
      printf("unknown host: %s\n",host.c_str());
      exit(1);
    }
    
    //build address structure
    sin.sin_family = AF_INET;
    bcopy(hp->h_addr,(char*)&sin.sin_addr,hp->h_length);
    sin.sin_port = htons(SERVER_PORT);
    
    //active open
    if ((sock= socket(PF_INET,SOCK_STREAM,IPPROTO_TCP))<0){
      printf("error in socket to destination\n");
      exit(1);
    }
    
    fputs(sendBuf,stdout);
    printf("\n");
    
   
    if (connect(sock,(struct sockaddr *)&sin,sizeof(sin))<0){
      printf("couldn't connect to the destination socket\n");
      exit(1);
    }
        
    int len = strlen(sendBuf)+1;
    send(sock,sendBuf,len,0);
    conn->status = READING_CLIENT;
  }

  

  // read packet=>
  //  set port
  //  set dest address
  //  set/extract dest path
  //  set serverSock
  
  
  // # forward packet # done in process_queue now

  // check if done reading packets
  //  yes => close connection
  //  no  => update status to READING_CLIENT
}

int forward(int src_sock, int dest_sock, int num_bytes = 2048)
{
  char buf[num_bytes];
  int len;
  
  printf("Trying to pull off of socket\n");
  len = recv(src_sock, buf, sizeof(buf), 0);
  printf("Length = %i\n", len);
  if (len){
    send(dest_sock, buf, len, 0);
  }
  return len;
}

void forward_next_packet_to_server(ProxyConnection* conn)
{
  if(! (forward(conn->clientSock, conn->serverSock))){
    printf("Switched to listening to server!\n");
    conn->status = READING_SERVER;
  }
}

void forward_next_packet_to_client(ProxyConnection* conn)
{
  if (! (forward(conn->serverSock, conn->clientSock))){
    printf("Finished transaction!\n");
    conn->status = COMPLETE;
  }
}

void *process_queue()
{
  while (true) {
    ProxyConnection* cur_connection = get_event();

    if (cur_connection->status == UNINITIALIZED) {
      printf("Starting init!\n");
      init_connection(cur_connection);
    }

    // start forwarding immediately
    if (cur_connection->status == READING_CLIENT) {
      printf("Continuing client reading!\n");
      forward_next_packet_to_server(cur_connection);
    }

    else if (cur_connection->status == READING_SERVER) {
      printf("Continuing server reading!\n");
      forward_next_packet_to_client(cur_connection);
    }


    // [Re]Enqueue if unfinished and free if finished
    if (cur_connection->status == COMPLETE) { 
      printf("Finished\n");
      free(cur_connection);
    }
    
    else {
      printf("Re-enqueing\n");
      enqueue_connection(cur_connection);
    }
  }
}


void spawn_event_processors(int count)
{
  std::thread threads[count];
  int i;
  for (i = 0; i < count; i++) {
    if (DEBUG > 1) printf("Thread %i Started\n", i+1);
    threads[i] = std::thread(process_queue);
  }
  print_break();
  for (i=0; i < count; i++) {
    threads[i].detach();
  }  
}

void print_break()
{
  printf("---------------------------------------------------------\n");
}

int main(int argc, char** argv)
{
  //Define port using commandline if given
  int port = DEFAULT_PORT;

  if (argc > 1){
    int user_port = atoi(argv[1]);

    if (user_port > 1024) {
      port = user_port; 
    }
    else {
      printf("Port must be > 1024\n");
      exit(1);
    }
  }

  print_break();
  printf("Proxy server port: %i\n", port);
  print_break();

  // Threads to process events
  spawn_event_processors(WORKER_THREADS);

  // Listen for incoming (client) connections
  listen_and_serve(port);
}
