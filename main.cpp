#include <list>
#include <string>

#include <signal.h>
#include <cstring>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "aux.h"


std::list<int> _clients;


#define EPOLL_SZ 10000

#define CHECK(f, ret)   \
          if ((f) < 0){ \
            perror(#f); \
            ret;        \
          }  

struct config{
  int port = 4555;
  int messSz = 12;
  int epfd = 0;
} cng;

int messageHandler(int client);

void closeHandler(int sig){
  if (cng.epfd){
    shutdown(cng.epfd, SHUT_RDWR);
  }
}

int main(int argc, char* argv[]){
  
  //////////////////////////////////////////
  //// args cli 
  if (argc > 1){
    if (std::string(argv[1]) == "--help"){
      printf("How use: ./serg [port = 4455] [messMaxSz = 188]\n");
      exit(0);
    } 
    if (isNumber(argv[1])){
      cng.port = atoi(argv[1]);
    }
  }
  if ((argc > 2) && isNumber(argv[2])){
    cng.messSz = atoi(argv[2]);
  }
  signal(SIGINT, closeHandler);
  signal(SIGTERM, closeHandler);
  
  //////////////////////////////////////////
  //// socket listener 
  int listener = 0;
  CHECK(listener = socket(PF_INET, SOCK_STREAM, 0), exit(-1));

  // nonblocking socket
  CHECK(fcntl(listener, F_SETFL, fcntl(listener, F_GETFD, 0) | O_NONBLOCK), exit(-1));

  sockaddr_in addr;

  addr.sin_family = PF_INET;
  addr.sin_port = htons(cng.port);
  addr.sin_addr.s_addr = inet_addr("0.0.0.0");

  socklen_t socklen;
  socklen = sizeof(sockaddr_in);

  // bind listener to address
  CHECK(bind(listener, (sockaddr*)&addr, sizeof(addr)), exit(-1));

  // start listen
  CHECK(listen(listener, 1), exit(-1));
  printf("start listen: %d\n", cng.port);

  //////////////////////////////////////////
  //// create epoll  
  CHECK(cng.epfd = epoll_create1(0), exit(-1));

  static epoll_event ev, events[EPOLL_SZ];

  ev.events = EPOLLIN | EPOLLET;
  
  // set listener to event template
  ev.data.fd = listener;

  // add listener to epoll
  CHECK(epoll_ctl(cng.epfd, EPOLL_CTL_ADD, listener, &ev), exit(-1));

  //////////////////////////////////////////
  //// epoll_wait cycle
  while(1){
    int eventsCount = epoll_wait(cng.epfd, events, EPOLL_SZ, -1);
    if (eventsCount == -1){
      if (errno == EINTR){
        continue;
      } 
      perror("epoll_wait");
      break;
    }    
    for(int i = 0; i < eventsCount; ++i){
      if(events[i].data.fd == listener){
        sockaddr_in clt_addr;
        int client = 0;
        CHECK(client = accept(listener, (sockaddr*)&clt_addr, &socklen), exit(-1));
        
        // nonblocking socket
        CHECK(fcntl(client, F_SETFL, fcntl(client, F_GETFD, 0) | O_NONBLOCK), exit(-1));

        // set new client to event template
        ev.data.fd = client;

        // add new client to epoll
        CHECK(epoll_ctl(cng.epfd, EPOLL_CTL_ADD, client, &ev), exit(-1));

        // save client
        _clients.push_back(client);
        
        // send initial welcome message to client
        std::string mess = "Welcome to chat! You is: Man #" + std::to_string(client) + "\n";
        CHECK(send(client, mess.c_str(), mess.size() + 1, 0), exit(-1));
      }   
      else if (events[i].events & (EPOLLHUP | EPOLLERR)){ 
        epoll_ctl(cng.epfd, EPOLL_CTL_DEL, events[i].data.fd, &ev);
        _clients.remove(events[i].data.fd);
        CHECK(close(events[i].data.fd), exit(-1));    
      }
      else if (events[i].events & EPOLLIN){ 
        messageHandler(events[i].data.fd);        
      }          
    }
  }
  shutdown(cng.epfd, SHUT_RDWR);
  close(listener);
  close(cng.epfd);

  return 0;
}

int messageHandler(int client){

  std::string mess,
              header = "Man #" + std::to_string(client) + " talk: '",
              footer = "'\n";
  
  size_t headSz = header.size(),
         footSz = footer.size(),
         maxMessSz = cng.messSz + headSz + footSz + 1;
  mess.resize(maxMessSz);
  
  // get new message from client
  int len = 0;
  CHECK(len = recv(client, (void*)mess.data(), cng.messSz + 1, 0), return -1);
  
  // the client sent more data than possible
  if (len == cng.messSz + 1){  
    do{
      len = recv(client, (void*)mess.data(), cng.messSz + 1, 0);
    }while(len == cng.messSz + 1);  
    std::string mess = "Server talk: 'Sent more data than possible!'\n";
    CHECK(send(client, mess.c_str(), mess.size() + 1, 0), return -1);
    return 0;
  }
  // send this message to everyone else
  else if (len > 0){
    if(_clients.size() == 1){
      std::string mess = "Server talk: 'None connected to server except you!'\n";
      CHECK(send(client, mess.c_str(), mess.size() + 1, 0), return -1);
      return 0;
    }else{    
      for(auto clt : _clients){
        if(clt != client){
          mess = header + std::string(mess.data(), len - 1) + footer;
          CHECK(send(clt, mess.c_str(), headSz + len + footSz, 0), return -1);
        }
      }
    }
  }
  return len;
}