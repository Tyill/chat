#include <list>
#include <string>
#include <strings.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

using namespace std;

list<int> _clients;

#define PORT 4555
#define MESS_MAX_SZ 188
#define EPOLL_SZ 10000

#define CALL(f) if(f < 0){    \
                  perror("f");\
                  exit(-1);   \
                }

int main(int argc, char *argv[]){
 
  int listener = 0;
  CALL((listener = socket(PF_INET, SOCK_STREAM, 0)));

  // nonblocking socket
  CALL(fcntl(listener, F_SETFL, fcntl(listener, F_GETFD, 0) | O_NONBLOCK));

  sockaddr_in addr, their_addr;

  addr.sin_family = PF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = inet_addr("0.0.0.0");

  socklen_t socklen;
  socklen = sizeof(struct sockaddr_in);

  // bind listener to address
  CALL(bind(listener, (sockaddr*)&addr, sizeof(addr)));

  // start listen
  CALL(listen(listener, 1));
  printf("start listen: %s!\n", PORT);

  int epfd = 0;
  CALL((epfd = epoll_create(EPOLL_SZ)));

  static epoll_event ev, events[EPOLL_SZ];

  ev.events = EPOLLIN | EPOLLET;
  
  // set listener to event template
  ev.data.fd = listener;

  // add listener to epoll
  CALL(epoll_ctl(epfd, EPOLL_CTL_ADD, listener, &ev));

  // epoll_wait cycle
  while(1){
    int eventsCount = epoll_wait(epfd, events, EPOLL_SZ, -1);
    for(int i = 0; i < eventsCount; ++i){

      if(events[i].data.fd == listener){
        int client = 0;
        CALL((client = accept(listener, (sockaddr*) &their_addr, &socklen)));

        // setup nonblocking socket
        CALL(fcntl(client, F_SETFL, fcntl(client, F_GETFD, 0) | O_NONBLOCK));

        // set new client to event template
        ev.data.fd = client;

        // add new client to epoll
        CALL(epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev));

        // save client
        _clients.push_back(client);
        
        // send initial welcome message to client
        char message[MESS_MAX_SZ];
        bzero(message, MESS_MAX_SZ);
        sprintf(message, "Welcome to chat! You ID is: Client #%d", client);
        CALL(send(client, message, MESS_MAX_SZ, 0));
      }
      else{ // EPOLLIN event for others (new incoming message from client)
        CALL(handle_message(events[i].data.fd));
      }
    }
  }
  close(listener);
  close(epfd);

  return 0;
}

int handle_message(int client){

  char message[MESS_MAX_SZ];
  bzero(message, MESS_MAX_SZ);

  // try to get new raw message from client
  int len = 0;
  CALL((len = recv(client, message, MESS_MAX_SZ, 0)));

  // client closed connection?
  if(len == 0){
    CALL(close(client));
    _clients.remove(client);
  }
  else{
    if(_clients.size() == 1){
      string mess = "Noone connected to server except you!";
      CALL(send(client, mess.c_str(), mess.size(), 0));
      return len;
    }    
    for(auto clt : _clients){
      if(clt != client){ 
        CALL(send(clt, message, MESS_MAX_SZ, 0));
      }
    }
  }
  return len;
}