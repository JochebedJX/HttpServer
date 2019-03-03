#include "threadpool.hpp"
#include "utils.hpp"
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <fcntl.h>
#include <signal.h>

using namespace std;

class HttpServer{
//建立一个TCP服务端程序，接收新连接
//为新连接组织一个线程池任务，添加到线程池中
private:
  int _serv_sock;
  ThreadPool* _tp;
  static bool (HttpHandler)(int sock)//任务处理函数，处理请求任务，回复任务
  {
    HttpRequest req(sock);
    HttpResponse rsp(sock);
    RequestInfo info;

    if(req.RecvHttpHeader(info)==false)
    {
      cerr<<"RecvHttpHeader error!"<<endl;
      goto out;
    }

    if(req.ParseHttpHeader(info)==false)
    {
      cerr<<"ParseHttpHeader error!"<<endl;
      goto out;
    }

    if(info.RequestIsCGI()==true)
    {
      rsp.CGIHandler(info);
    }
    else
    {
      rsp.FileHandler(info);
    }

out: 
  rsp.ErrHandler(info);
  close(sock);
  return true;

}

public:
  //HttpServer(int sock):_serv_sock(sock),_tp(NULL){}

  bool HttpServerInit(const string& ip,uint16_t port)//完成TCP服务端socket的初始化，创建，绑定，监听，新建一个线程池，线程池的初始化ThreadPoolInit()
  {
    _serv_sock=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(_serv_sock < 0){
      cerr<<"use socket"<<endl;
      return false;
    }
    
    int ov = 1;
    setsockopt(_serv_sock, SOL_SOCKET, SO_REUSEADDR, (void*)&ov, sizeof(ov));
    
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=inet_addr(ip.c_str());
    if(bind(_serv_sock, (sockaddr*)&addr, sizeof(addr)) < 0){
      cerr<<"use bind"<<endl;
      return false;
    }

		int backlog=20;
    if(listen(_serv_sock, backlog) < 0){
      cerr<<"use listen"<<endl;
      return false;
    }

    _tp = new ThreadPool(MAX_THREAD);
		if(_tp->ThreadPoolInit() == false){
			cerr<<"thread pool init error!"<<endl;
		}

    return true;
  }
  bool Start()//开始获取客户端新连接，socket接受请求，创建任务（SetHttpTask），任务入队（PushTask）
  {
    while(1){
      sockaddr_in peer_addr;
      socklen_t len = sizeof(peer_addr);
      int new_sock = accept(_serv_sock, (sockaddr*)&peer_addr, &len);
      LOG("new_sock:%d\n",new_sock);
      if (new_sock < 0) {
        cerr<<"accept"<<endl;
        return false;
      }

      HttpTask tt;
      tt.SetHttpTask(new_sock,HttpHandler);
      _tp->PushTask(tt);
    }
    return true;
  }
};

void UserTip(char* str){
  cout << "please input " << str << " ip port !!" << endl;
}

int main(int argc, char* argv[])
{
  if(argc <= 2)
  UserTip(argv[0]);

  std::string ip = argv[1];
  uint16_t port = atoi(argv[2]);
  
  HttpServer server;

  signal(SIGPIPE,SIG_IGN);

  if(server.HttpServerInit(ip, port) == false)
    return -1;

  cout << "server start!!" <<endl;
  if(server.Start() == false)
    return -1;

  return 0;
}


