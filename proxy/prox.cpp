/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C programming language under Linux.
 */

#include <stdlib.h>
#include <stdio.h>

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


//Prints a line of ----'s
void print_break();


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


ProxyConnection* get_event(){
  event_lock.lock();
  while(event_queue.empty()){}
  ProxyConnection* next_event = event_queue.front(); 
  event_queue.pop(); 
  event_lock.unlock();
  return next_event;
}

int listen(int port){
}

void *process_connection(){
  while(true){
    ProxyConnection* cur_connection = get_event();
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
  print_break();

  //Define port using commandline if given
  int port = DEFAULT_PORT;
  if (argc > 1){
    int user_port = atoi(argv[1]);
    if (user_port > 1000) port = user_port; 
    else printf("Port entered not recognized\n");
  }
  printf("Proxy server port: %i\n", port);
  print_break();

  // Threads to process events
  spawn_event_processors(NUM_THREADS);

  // Listen for incoming (client) connections
  listen(port);

}
