/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C programming language under Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

//number of clients that can be in the backlog
#define MAX_BACKLOG 10

//Because C sucks
#define true 1 

//size of the buffer for requests
#define REQ_SIZ 2048

#define NUM_THREADS 1

// proxy connection struct 
struct ProxyConnection{
  int clientSoc;
  int serverSoc;
  int destPort;
  char * destAddr;
  int command; 
  int status; // listerning, reading from client, or sending to client
};

int listen(int port){
}

void *process_connection(){
  return;
}


void spawn_event_processors(int count) {
  pthread_t threads[count];
  int i;
  for (i = 0; i < count; i++){
    pthread_create(&threads[i], NULL, process_connection,NULL);
  }
  for (i=0; i < count; i++) {
    pthread_join(threads[i], NULL);
  }
  
}

int main(int argc, char** argv){
  // Threads to process events
  spawn_event_processors(NUM_THREADS);

  // Listen for incoming (client) connections
  listen(6555);
}
