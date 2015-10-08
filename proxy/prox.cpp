/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C++ programming language under Linux.
 */


/***********************************************************************
  So we were wrong with the recieves.
  The connection is not finished so that recv will not return 0.
  It only does that when the socket is closed!
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
#include <unistd.h>
#include <signal.h>

// number of clients that can be in the backlog
#define MAX_BACKLOG 10

// size of the buffer for requests
#define REQ_SIZ 2048

// Number of threads that'll be doing stuff
#define WORKER_THREADS 4

#define DEFAULT_PORT 8000

#define DEBUG 2 // 0 = No Debugging, 1 = Some, 2 = Full

// Connection status constants
#define COMPLETE -1
#define UNINITIALIZED 0
#define MAILING 1

int SERVER_PORT = 80;

struct ProxyConnection{
  int clientSock;
  int serverSock;
  int status;
};

std::queue<ProxyConnection*> event_queue;
std::mutex event_lock;
int threads_running = 1; // Boolean to say if threads should be running

// Prints a line of ----'s
void print_break();

void free_connection(ProxyConnection* conn){
  shutdown(conn->clientSock,2);
  shutdown(conn->serverSock,2);
  free(conn);
}

void graceful_end(int signum){
  threads_running = 0;
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
    printf("failed to bind.\n");
    graceful_end(1);
  }

  // Listen on the socket
  listen(listen_sock, MAX_BACKLOG);

  // Handle creation of sockets for incoming connections
  serve(listen_sock, my_addr);
}

void init_connection(ProxyConnection* conn)
{
  char buf[REQ_SIZ];
  int len;
  struct hostent *hp;
  struct sockaddr_in sin;
  int sock;
  

  if ((len = recv(conn->clientSock,buf,sizeof(buf),0))){

    std::string send_buf = "";
    std::string host = "";
    std::string port = "";
    size_t content_len = 0;
    size_t i = 0;
    
    // Clear code structure with comments. DO NOT CHANGE.
    do send_buf.push_back(buf[i]); while(buf[i++] != ' ');          //Copy command
    
    while(buf[i++] != ':'); 
    i+=2;                                                           //Skip http or https
    
    while(buf[i] != '/' && buf[i] != ':') host.push_back(buf[i++]); //Copy host name

    if(buf[i] == ':'){
      i++;
      while(buf[i] != '/') port.push_back(buf[i++]);
    } 
    
    do send_buf.push_back(buf[i]); while(buf[i++] != ' ');          //Copy rest of address 
    while(buf[i++] != '\n');                                        //Skip HTTP/1.1
    send_buf += std::string("HTTP/1.0\n");                          //Add HTTP/1.0
    send_buf += std::string(&buf[i]);                               //Finish copy
    send_buf.replace(send_buf.find("Connection: keep-alive"), 22, "Connection: close");
    
    if(!port.empty()){
      SERVER_PORT = atoi(port.c_str());
    }
    
    //translate the host name into IP address
    hp = gethostbyname(host.c_str());
    if (!hp){
      printf("unknown host: %s\n",host.c_str());
      conn->status = COMPLETE;
      return;
    }
    
    //build address structure
    sin.sin_family = AF_INET;
    bcopy(hp->h_addr,(char*)&sin.sin_addr,hp->h_length);
    sin.sin_port = htons(SERVER_PORT);
    
    //active open
    if ((sock= socket(PF_INET,SOCK_STREAM,IPPROTO_TCP))<0){
      printf("error in socket to destination\n");
      graceful_end(1);
    }
    conn->serverSock = sock;


    /*
    print_break();
    fputs(buf,stdout);
    print_break();
    fputs(send_buf.c_str(),stdout);
    printf("\n");
    */
   
    if (connect(sock,(struct sockaddr *)&sin,sizeof(sin))<0){
      printf("couldn't connect to the destination socket\n");
      graceful_end(1);
    }
        
    int len = strlen(send_buf.c_str())+1;

    if (sendto(sock,send_buf.c_str(),len,0, (struct sockaddr *) &sin, sizeof(sin)) < 0){
      printf("Error in initial send\n");
      graceful_end(1);
    }
    conn->status = MAILING;
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

int forward(int src_sock, int dest_sock)
{
  char buf[REQ_SIZ];
  int cli_len;
  int serv_len;
  
  cli_len = recv(src_sock, buf, sizeof(buf), MSG_DONTWAIT);
  if (cli_len > 0){
    send(dest_sock, buf, cli_len, 0);
  }
  
  serv_len = recv(dest_sock, buf, sizeof(buf), MSG_DONTWAIT);
  if (serv_len > 0){
    send(src_sock, buf, serv_len, 0);
  }

  //printf("Client: %i <--------> %i : Server\n",cli_len,serv_len);

  return cli_len != 0 && serv_len != 0;
}

void *process_queue()
{
  while (threads_running) {
    ProxyConnection* cur_connection = get_event();

    if (cur_connection->status == UNINITIALIZED) {
      //printf("Starting init!\n");
      init_connection(cur_connection);
    }

    else if (cur_connection->status == MAILING) {
      //printf("Mailing Packets back and forth!\n");
      if (!forward(cur_connection->clientSock, cur_connection->serverSock)){
	cur_connection->status = COMPLETE;
      }
    }


    if (cur_connection->status == COMPLETE) { 
      //printf("Finished\n");
      free_connection(cur_connection);
    }
    
    else {
      //printf("Re-enqueing\n");
      enqueue_connection(cur_connection);
    }
  }
}


void spawn_event_processors()
{
  std::thread threads[WORKER_THREADS];
  int i;
  for (i = 0; i < WORKER_THREADS; i++) {
    if (DEBUG > 1) printf("Thread %i Started\n", i+1);
    threads[i] = std::thread(process_queue);
  }
  print_break();
  for (i=0; i < WORKER_THREADS; i++) {
    threads[i].detach();
  }  
}

void print_break()
{
  printf("---------------------------------------------------------\n");
}

int main(int argc, char** argv)
{
  signal(SIGINT, graceful_end);

  //Define port using commandline if given
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
