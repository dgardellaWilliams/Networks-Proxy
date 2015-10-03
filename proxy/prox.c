/**
 * (c) 2015 Devin Gardella, Matt LaRose, and Diwas Timilsina 
 *
 *   A simple web proxy in the C programming language under Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <poll.h>


//number of clients that can be in the backlog
#define MAX_BACKLOG 10

//Because C sucks
#define true 1 

//size of the buffer for requests
#define REQ_SIZ 2048

int listener(int port){
  int listSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in my_addr;
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  
  int uno = 1;
  setsockopt(listSock,SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno) == -1);
  if (bind(listSock, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) exit(1);    
  listen(listSock, MAX_BACKLOG);

  // This infinite while loop handles all server operations
  while(true){
    // This is where new connections are accepted
    int newSock = 0;
    int addrlenp = sizeof(my_addr);
    char *buf = (char*) malloc (REQ_SIZ);
    //If we reach a connection attempt. 
    if ((newSock = accept(listSock,(struct sockaddr *)&my_addr,(socklen_t *)&addrlenp)) > 0 ) {  
      
      free(buf);
    }      
  }
}


void spawn_event_processors(int count) {
  while (count--) {
    // spawn event processor thread
  }
}

int main(int argc, char** argv){
  // Threads to process events
  spawn_event_processors(4);

  // Listen for incoming (client) connections
  listen();
}
