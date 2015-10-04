/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C programming language under Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>

#include <thread>
#include <string>
#include <queue>
#include <mutex>


//number of clients that can be in the backlog
#define MAX_BACKLOG 10

//size of the buffer for requests
#define REQ_SIZ 2048

#define NUM_THREADS 1

#define DEFAULT_PORT 65565

#define DEBUG 2 //0 = No Debugging, 1 = Some, 2 = Full

// proxy connection struct 
struct ProxyConnection{
  int clientSoc;
  int serverSoc;
  int destPort;
  char * destAddr;
  int command; 
  int status; // listerning, reading from client, or sending to client
};

std::queue<ProxyConnection*> event_queue;
std::mutex event_lock;
int connections_open;

//Prints a line of ----'s
void print_break();

ProxyConnection* get_event(){
  event_lock.lock();

  while(event_queue.empty()){}
  ProxyConnection* next_event = event_queue.front(); 
  event_queue.pop(); 

  event_lock.unlock();
  return next_event;
}

int front_listen(int port){
  //Create the socket
  int front_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  
  struct sockaddr_in my_addr;
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int true_val = 1;
  //Set the socket to allow the reuse of local addresses
  setsockopt(front_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &true_val, sizeof(true_val));

  //Bind socket and exit if failed.
  if (bind(front_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) exit(1);
 
  //Listen on the socket
  listen(front_sock, MAX_BACKLOG);

  while(true){
    int new_cli_sock = 0;
    int addr_len = sizeof(my_addr);
    
    //When we get a new connection, we pop in onto the queue!
    if( (new_cli_sock = accept(front_sock,(struct sockaddr *)&my_addr,(socklen_t *)&addr_len))) {
      ProxyConnection* new_connect = (ProxyConnection*) malloc(sizeof(ProxyConnection));
      new_connect->clientSoc = new_cli_sock;
      new_connect->status = 0;
      
      event_queue.push(new_connect);
      connections_open++;
    }
  }

}

void *process_connection(){
  while(true){
    ProxyConnection* cur_connection = get_event();
    if (cur_connection->status == 0){

    }

    //Enqueue if unfinished and free if finished
    if (cur_connection->status == -1) free(cur_connection);
    else event_queue.push(cur_connection);
  }
}


void spawn_event_processors(int count) {
  std::thread threads[count];
  int i;
  for (i = 0; i < count; i++){
    if (DEBUG > 1) printf("Thread %i Started\n",i+1);
    threads[i] = std::thread(process_connection);
  }
  print_break();
  for (i=0; i < count; i++) {
    threads[i].join();
  }  
}

void print_break(){
  printf("---------------------------------------------------------\n");
}

int main(int argc, char** argv){
  //Define port using commandline if given
  int port = DEFAULT_PORT;
  if (argc > 1){
    int user_port = atoi(argv[1]);
    if (user_port > 1000) port = user_port; 
    else printf("Port entered not recognized\n");
  }

  print_break();
  printf("Proxy server port: %i\n", port);
  print_break();

  // Threads to process events
  spawn_event_processors(NUM_THREADS);

  // Listen for incoming (client) connections
  front_listen(port);

}
