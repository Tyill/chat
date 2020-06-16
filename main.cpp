#include <list>
#include <string>
#include <chrono>

#include <signal.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "aux.h"


#define EPOLL_SZ 10000

#define CHECK(f, ret)   \
          if ((f) < 0){ \
            perror(#f); \
            ret;        \
          }  

struct config{
  int port = 4555;
  int messSz = 180;
  int epfd = 0;
  int toutMS = 1000;
} cng;

struct client{
  int fd;
  std::chrono::_V2::high_resolution_clock::time_point tmPoint;
};
std::list<client> _clients;

int messageHandler(client& client);

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
      printf("How use: ./serg [port] [messMaxSz] [toutMS]\n");
      exit(0);
    } 
    if (isNumber(argv[1])){
      cng.port = atoi(argv[1]);
    }
  }
  if ((argc > 2) && isNumber(argv[2])){
    cng.messSz = atoi(argv[2]);
  }
  if ((argc > 3) && isNumber(argv[3])){
    cng.toutMS = atoi(argv[3]);
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
        int hClient = 0;
        CHECK(hClient = accept(listener, (sockaddr*)&clt_addr, &socklen), exit(-1));
        
        // nonblocking socket
        CHECK(fcntl(hClient, F_SETFL, fcntl(hClient, F_GETFD, 0) | O_NONBLOCK), exit(-1));

        // set new client to event template
        ev.data.fd = hClient;

        // add new client to epoll
        CHECK(epoll_ctl(cng.epfd, EPOLL_CTL_ADD, hClient, &ev), exit(-1));

        // save client
        _clients.push_back(client{hClient});
        
        // send initial welcome message to client
        std::string mess = "Welcome to chat! You is: Man #" + std::to_string(hClient) + "\n";
        CHECK(send(hClient, mess.c_str(), mess.size() + 1, 0), exit(-1));
      }   
      else if (events[i].events & (EPOLLHUP | EPOLLERR)){ 
        epoll_ctl(cng.epfd, EPOLL_CTL_DEL, events[i].data.fd, &ev);
        int fd = events[i].data.fd;
        _clients.remove_if([fd](const client& clt){
          return fd == clt.fd;
        });
        CHECK(close(events[i].data.fd), exit(-1));    
      }
      else if (events[i].events & EPOLLIN){ 
        int fd = events[i].data.fd;
        auto itClt = std::find_if(_clients.begin(), _clients.end(), [fd](const client& clt){
          return fd == clt.fd;
        });
        messageHandler(*itClt);        
      }          
    }
  }
  shutdown(cng.epfd, SHUT_RDWR);
  close(listener);
  close(cng.epfd);

  return 0;
}

int messageHandler(client& sender){

  std::string mess;
  mess.resize(cng.messSz + 1);
  
  // get new message from client
  int lenMess = 0, count = 0;
  do{
    count = recv(sender.fd, (void*)mess.data(), cng.messSz + 1, 0);
    if (count > 0){ 
      lenMess += count;
    }
  }while(count == cng.messSz + 1);

  // client sent more data than possible?
  if (lenMess > cng.messSz + 1){
    std::string mess = "Server talk: 'Sent more data than possible!'\n";
    CHECK(send(sender.fd, mess.c_str(), mess.size() + 1, 0), return -1);
    return 0;
  } 

  // client disconnect?
  if (lenMess == 0){
    epoll_ctl(cng.epfd, EPOLL_CTL_DEL, sender.fd, nullptr);
    _clients.remove_if([&sender](const client& clt){
      return sender.fd == clt.fd;
    });
    CHECK(close(sender.fd), exit(-1));  
    return 0;
  }
   
  // send this message to everyone else
  if (lenMess > 0){
    
     // check timeout
    auto cTm = std::chrono::high_resolution_clock::now();
    if ( std::chrono::duration<double, std::milli>(cTm - sender.tmPoint).count() < cng.toutMS){
      std::string mess = "Server talk: 'You can’t send messages so often!'\n";
      CHECK(send(sender.fd, mess.c_str(), mess.size() + 1, 0), return -1);
      return 0;
    }
    sender.tmPoint = cTm;
    
    if(_clients.size() == 1){
      std::string mess = "Server talk: 'None connected to server except you!'\n";
      CHECK(send(sender.fd, mess.c_str(), mess.size() + 1, 0), return -1);
      return 0;
    }
    else{ 
      std::string header = "Man #" + std::to_string(sender.fd) + " talk: '",
                  footer = "'\n";  
      size_t headSz = header.size(),
             footSz = footer.size();
      for(auto& clt : _clients){
        if(clt.fd != sender.fd){
          std::string smess = header + std::string(mess.data(), lenMess - 1) + footer;
          CHECK(send(clt.fd, smess.c_str(), headSz + lenMess + footSz, 0), return -1);
        }
      }
    }
  }
  return lenMess;
}