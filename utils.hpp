#pragma once

#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <errno.h>
#include <sstream> 
#include <dirent.h>
#include <fcntl.h>
#include<time.h>

using namespace std;

#define LOG(...) do{ fprintf(stdout, __VA_ARGS__); fflush(stdout);}while(0)

#define WWWROOT "www"
#define MAX_HTTPHDR 4096 //所能接收http头部的最大长度
#define MAX_PATH 256
#define MAX_BUFF 4096

std::unordered_map<std::string, std::string> g_err_desc = {
  {"200", "OK"},
  {"400", "Bad Quest"},
  {"403", "Forbidden"},
  {"404", "Not Found"},
  {"405", "Method Not Allowed"},
  {"413", "Request Entity Too Large"},
};

//文件的类型
std::unordered_map<std::string, std::string> g_mime_type = {
  {"txt", "application/octet-stream"},
  {"html", "text/html"},
  {"htm", "text/html"},
  {"jpg", "image/jpeg"},
  {"zip", "application/zip"},
  {"mp3", "audio/mpeg"},
  {"mpeg", "video/mpeg"},
  {"unknow", "application/octet-stream"},
};

class RequestInfo
{
  //包含HttpRequest解析出来的请求信息
  public:
    string _method;//请求方法
    string _version;//请求协议版本
    string  _path_info;//资源路径
    string _path_phys;//资源实际路径
    string _query_string;//查询字符串
    unordered_map<string,string> _hdr_list;//整个头部信息键值对
    struct stat _st;//获取文件信息

    string _err_code;
  public:
    void SetError(const std::string str)
    {
      _err_code = str;
    }

    bool RequestIsCGI()//判断请求是否是CGI请求（CGI 通用网关接口，用于执行外部程序，创建子进程，通过程序替换将CGI用来执行外部程序
    {
      if ((_method == "GET" && !_query_string.empty()) || (_method == "POST"))
      {
        return  true;
      }
      return false;
    }

};

class Utils
{
  //提供一些公用的功能接口
  public:
    static int Split(string& src,const string& seg, vector<string>& list)//分块
    {
      int num=0;
      size_t index=0;
      size_t pos;

      while(index<src.length())
      {
        pos=src.find(seg,index);
        if(pos==string::npos)
        {
          break;
        }

        list.push_back(src.substr(index,pos-index));
        num++;
        index=pos+seg.length();
      }

      if(index<src.length())
      {
        list.push_back(src.substr(index,pos-index));
        num++;
      }
      return num;
    }

    //Last-Modified
    static void TimeToGmt(time_t t, string& mtime)
    {
      struct tm* mt=gmtime(&t);
      char tmp[128]={0};
      int len=strftime(tmp, 127, "%a, %d %b %Y %H:%M:%S GMT",mt);
      mtime.assign(tmp,len);
    }
    //ETag : 文件的ETag : ino--size--mtime 
    static void MakeETag(int64_t ino, int64_t size, int64_t mtime, std::string& etag)
    {
      //"ino-size-mtime"
      stringstream ss;//字符串流，用于将int形的变量转化成string，还可以从string转成int形
      ss << "\"" << std::hex << ino << "-" << std::hex << size << "-" << std::hex << mtime << "\"";
      etag = ss.str();
    }
    //fsize
    static void DigitToStr(int64_t num, string& str)
    {
      stringstream ss;
      ss<<num;
      str=ss.str();
    }
    static int64_t StrToDigit(const std::string& str)
    {
      int64_t num;
      std::stringstream ss;
      ss << str;
      ss >> num;
      return num;
    }
    //获取文件类型
    static void GetMime(const string &path, string& mime)
    {
      size_t pos = path.find_last_of(".");
      if (pos == string::npos)
      {
        mime = g_mime_type["unknow"];
        return;

      }
      //后缀
      string suffix = path.substr(pos + 1);
      auto it = g_mime_type.find(suffix);
      if (it == g_mime_type.end())
      {
        mime = g_mime_type["unknow"];
        return;
      }
      else
      {
        mime = it->second;
      }
    }

    static void DigitToStrFsize(double num, string& str)
    {
      stringstream ss;
      ss << num;
      str = ss.str();
    }
    static const std::string GetErrDesc(std::string &code)
    {

      auto it = g_err_desc.find(code);
      if (it == g_err_desc.end())
      {
        return "Unknown Error";
      }

      return it->second;
    }
};

class HttpRequest
{
  //HTTP数据的接收
  //HTTP数据的解析接口
  //对外提供能够获取处理结果的接口
  private:
    int _cli_sock;
    string _http_header;
    RequestInfo _req_info;
  public:
    HttpRequest(int sock):_cli_sock(sock){}
    bool RecvHttpHeader(RequestInfo& info)//接收HTTP请求头
    {
      char buf[MAX_HTTPHDR];//限定头的长度
      //先判断接受的请求是否合法
      while(1)
      {
        size_t rcv=recv(_cli_sock,buf,MAX_HTTPHDR,MSG_PEEK);
        if(rcv<=0)
        {
          if(errno==EINTR||errno==EAGAIN)//EINTR指被信号打断，SAGAIN是指当前缓冲区无数据
          {
            continue;
          }
          info.SetError("500");
          return false;
        }

        //接收请求头
        //ptr为NULL表示buf里面没有"\r\n\r\n"(空行)
        char* ptr = strstr(buf, "\r\n\r\n");
        //当读了MAX_HTTPHDR这么多的字节，但是还是没有把头部读完，即ptr为空，说明头部过长了
        if ((ptr == NULL) && (rcv == MAX_HTTPHDR))
        {
          info.SetError("413");
          return false;
        }
        //当读的字节小于这么多，并且没有空行出现，说明数据还没有从发送端发送完毕，所以接收缓存区，需要等待一下再次读取数据
        else if ((ptr == NULL) && (rcv < MAX_HTTPHDR))
        {
          usleep(1000);
          continue;
        }

        //收到数据,包括请求行、请求头部、请求正文，我们只要空行之前的
        int head_len=ptr-buf;//请求行+请求头部
        _http_header.assign(buf, head_len);
        recv(_cli_sock, buf, head_len + 4, 0);//加上空行？？？？？？？？？
        LOG("header:\n%s\n", _http_header.c_str());

        break;
      } 
      return true;
    }
    void PathIsLegal(string& path_info, RequestInfo& info)
    {
      string file = WWWROOT + path_info;
      char tmp[MAX_PATH]={0};
      realpath(file.c_str(),tmp);//绝对路径将保存到tmp中
      info._path_phys=tmp;

      //判断相对路径是否可访问且合法
      //iii判断相对路径，防止出现就是将相对路径改为绝对路径的时候，这个绝对路径中没有根目录了
      //也就是访问权限不够了
      if (info._path_phys.find(WWWROOT) == std::string ::npos)
      {
        info._err_code = "403";
        return;
      }

      //stat函数，通过路径获取文件信息
      //stat函数需要物理路径获取文件的信息，而不是需要相对路径
      //看有无此文件，即是否合法
      if (stat(info._path_phys.c_str(), &(info._st)) < 0)
      {
        LOG("_path_phys:%s\n",info._path_phys.c_str());
      LOG("_st:%d\n%d\n%d\n",info._st.st_ino,info._st.st_mtim,info._st.st_size);
        info._err_code = "404";
        return;
      }
    }

    bool ParseFirstLine(string& line, RequestInfo& info)//解析请求行
    {
      ////请求方法
      //std::string _method;
      ////协议版本
      //std::string _version;
      ////资源路径
      //std::string _path_info;
      ////资源实际路径
      //std::string _path_phys;
      ////查询字符串
      //std::string _query_string;<Paste>    

      vector<string> list;
      if(Utils::Split(line," ",list)!=3)
      {
        info._err_code="400";
        return false;
      }

      string url;
      info._method=list[0];
      url=list[1];
      info._version=list[2];

      //解析url
      if(info._method!="GET"&&info._method!="POST"&&info._method!="HEAD")
      {
        info._err_code="405";
        return false;
      }
      if(info._version!="HTTP/0.9"&&info._version!="HTTP/1.0"&&info._version!="HTTP/1.1")
      {
        info._err_code="400";
        return false;
      }

      //url : /upload?key=val&key=val
      size_t pos=url.find("?");
      if(pos==string::npos)
      {
        info._path_info=url;
      }
      else{
        info._path_info=url.substr(0,pos);
        info._query_string=url.substr(pos+1);
      }
      LOG("_query_string:%s\n",info._query_string.c_str());
      PathIsLegal(info._path_info, info);
      //realpath()将相对路径转换为绝对路径，如果不存在直接崩溃
      //info._path_phys = WWWROOT + info._path_info;
      return true;
    }
    bool ParseHttpHeader(RequestInfo& info)//解析HTTP请求头，（解析完成之后，判断RequestIsCGI()，若是则执行CGIHandler()，否则执行FileHandler()）
    {
      //http请求头解析
      //请求方法 URL 协议版本\r\n
      //key: val\r\nkey: val
      vector<string> head_list;
      int num=Utils::Split(_http_header, "\r\n", head_list);//分块
      ParseFirstLine(head_list[0], info);//解析请求行
      //将首行删除
      //hdr_list.erase(head_list.begin());
      //将所有的头部key: val进行存放
      for (size_t i = 1; i < head_list.size(); i++)
      {
        size_t pos = head_list[i].find(": ");
        info._hdr_list[head_list[i].substr(0, pos)] = head_list[i].substr(pos + 2);
      }

      return true;
    }

    RequestInfo& GetRequestInit();//向外提供解析接口
};

class HttpResponse
{
  //文件请求接口(完成文件下载，列表功能）
  //CGI请求接口（调用外部程序处理接口)
  private:
    int _cli_sock;
    string _etag;//文件是否被修改
    string _mtime;//文件最后被修改时间
    string _cont_len;
    string _date;//GMT时间
    string _fsize;//文件大小
    string _mime;//文件类型

  public:
    HttpResponse(int sock):_cli_sock(sock){}
    bool InitResponse(RequestInfo& req_info)//初始化一些响应信息
    {
      //Last-Modified
      Utils::TimeToGmt(req_info._st.st_mtime, _mtime);
      //ETag : 文件的ETag : ino--size--mtime 
      Utils::MakeETag(req_info._st.st_ino, req_info._st.st_size, req_info._st.st_mtime, _etag);
      //Date : 文件最后响应时间
      time_t t = time(NULL);
      Utils::TimeToGmt(t, _date);
      //fsize
      Utils::DigitToStr(req_info._st.st_size, _fsize);
      //_mime : 文件类型
      Utils::GetMime(req_info._path_phys, _mime);
      return true;
    }
    bool SendData(string buf)
    {
      ssize_t s = send(_cli_sock,buf.c_str(),buf.length(),0);
      if(s<0)
      {
        return false;
      }
      return true;
    }
    bool SendCData(string buf)
    {
      if (buf.empty())
      {
        //最后一个分块
        SendData("0\r\n\r\n");
      }
      std::stringstream ss;
      ss << std::hex << buf.length() << "\r\n";
      SendData(ss.str());
      ss.clear();
      SendData(buf);
      SendData("\r\n");

      return true;
    }

    bool ProcessFile(RequestInfo& info)//文件下载
    {
      cout<<"In ProcessFile"<<endl;
      string header=info._version+" 200 OK\r\n";
      header+="Content-Type: "+_mime+"\r\n";
      header+="Connection: close\r\n";
      header+="Content-Length: "+_fsize+"\r\n";
      header+="ETag: "+_etag+"\r\n";
      header+="Date: "+_date+"\r\n";
      header+="Last-Modified: "+_mtime+"\r\n\r\n";
      SendData(header);

      LOG("headerfile:[%s]\n",header.c_str());

      int fd = open(info._path_phys.c_str(), O_RDONLY);
      if (fd < 0)
      {
        info._err_code = "400";
        ErrHandler(info);
        return false;
      }
      int rlen = 0;
      char tmp[MAX_BUFF];
      while ((rlen = read(fd, tmp, MAX_BUFF)) > 0)
      {
        //使用这样子发送的话就会导致，服务器挂掉
        //不能这样子发送，如果这样子发送的话，就是将数据转化为string类型的了
        //如果文本中存在\0的话，就会导致每次发送的数据没有发送完毕
        //有这样的一种情况，文本中的数据都是\0那么就会在第一次发送过去的时候，发送0个数据
        //tmp[rlen + 1] = '\0';
        //SendData(tmp);
        //发送文件数据的时候不能用string发送
        //对端关闭连接，发送数据send就会收到SIGPIPE信号，默认处理就是终止进程
        send(_cli_sock, tmp, rlen, 0);
      }
      close(fd);
      return true;

    }
    bool ProcessList(RequestInfo& info)//文件列表
    {
      string header=info._version+" 200 OK\r\n";
      header+="Connection: close\r\n";
      if (info._version == "HTTP/1.1")
      {
        //只有HTTP版本是1.1的时候才可以使用Transfer-Encoding：chunked进行传输
        header += "Transfer-Encoding: chunked\r\n";
      }
      header+="ETag: "+_etag+"\r\n";
      header+="Date: "+_date+"\r\n";
      header+="Last-Modified: "+_mtime+"\r\n\r\n";
      SendData(header);
      
      string body;
      body="<html><head>";
      body+="<title>Jochebed`s Server" + info._path_info + "</title>";
      //meta是一个html页面中的元信息，可自己调整
      body+="<meta charset='UTF-8'>";
      body+="</head><body>";
      //<hr />是一个横线
      body += "<h1>Index of"+ info._path_info;
      body += "</h1>";
      //form表单为了出现上传按钮,请求的资源是action,请求的方法是POST
      body += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
      //测试想要上传两个文件
      //body += "<input type='file' name='FileUpload' />";
      body += "<input type='file' name='FileUpload' />";
      body += "<input type='submit' value='上传' />";
      body += "</form>";
      //<ol>是进行排序
      body += "<hr /><ol>";

      SendCData(body);

      //scandir函数，获取目录下的每一个文件，组织出html信息，chunk传输
      struct dirent** p_dirent = NULL;//struct dirent{long d_ino; /* inode number 索引节点号 */  off_t d_off; /* offset to this dirent 在目录文件中的偏移 */ unsigned short d_reclen; /* length of this d_name 文件名长 */  unsigned char d_type; /* the type of d_name 文件类型 */  char d_name [NAME_MAX+1]; /* file name (null-terminated) 文件名，最长255字符 */  }  
      //第三个参数为NULL表示对于该目录下的文件都进行查找不进行过滤,并且将所有查找出来的文件放到目录结构dirent的这个里面
      int num = scandir(info._path_phys.c_str(), &p_dirent, NULL, alphasort);
      for (int i = 0; i < num ; i++)
      {
        string file_html;
        string file_path;
        file_path += info._path_phys + p_dirent[i]->d_name;
        //存放这个文件信息
        struct stat st;
        //获取文件信息
        if (stat(file_path.c_str(), &st) < 0)
        {
          continue;
        }
        string mtime;
        Utils::TimeToGmt(st.st_mtime, mtime);
        string mime;
        Utils::GetMime(p_dirent[i]->d_name, mime);
        string fsize;
        Utils::DigitToStrFsize(st.st_size / 1024, fsize);
        //给这个页面加上了一个href+路径，一点击的话就会连接，进入到一个文件或者目录之后会给这个文件或者目录的网页地址前面加上路径
        //比如，列表的时候访问根目录下的所有文件
        //_path_info就变成了-- /. -- /.. -- /hello.dat -- /html -- /test.txt
        //然后在网页中点击的时候，向服务器发送请求报文
        //请求的路径:[info._path_info/文件名],然后服务器返回一个html页面，对于多次的话，网页会进行缓存有的时候就会直接进行跳转，不在向服务器发送http请求
        //直接根据这个网页路径来进行跳转网页，这就是网页缓存
        file_html += "<li><strong><a href='"+ info._path_info;
        file_html += p_dirent[i]->d_name;
        file_html += "'>";
        //打印名字 
        file_html += p_dirent[i]->d_name;
        file_html += "</a></strong>";
        file_html += "<br /><small>";
        file_html += "modified: " + mtime + "<br />";
        file_html += mime + " - " + fsize + "kbytes";
        file_html += "<br /><br /></small></li>";
        SendCData(file_html);
      }
      body = "</ol><hr /></body></html>";
      SendCData(body);
      //进行分块发送的时候告诉已经发送完毕了，不用再让客户端进行一个等待正文的过程了
      SendCData("");

      return true;
    }
    bool ErrHandler(RequestInfo &info)//处理错误响应
    {
      std::string header;
      std::string rsp_body;
      //首行 协议版本 状态码 状态描述\r\n
      //头部 Content-Length Date 
      //空行 
      //正文 rsp_body = "<html><body><h1>404<h1></body></html"
      header = info._version;
      header += " " + info._err_code + " ";
      header += Utils::GetErrDesc(info._err_code) + "\r\n";

      time_t t = time(NULL);
      std::string gmt;
      Utils::TimeToGmt(t, gmt);
      header += "Date: " + gmt + "\r\n";
      std::string cont_len;
      rsp_body = "<html><body><h1>" + info._err_code + "<h1></body></html>";
      Utils::DigitToStr(rsp_body.length(), cont_len);
      header += "Content-Length: " + cont_len + "\r\n\r\n";

      send(_cli_sock, header.c_str(), header.length(), 0);
      send(_cli_sock, rsp_body.c_str(), rsp_body.length(), 0);
      return true;
    }
    bool ProcessCGI(RequestInfo& info)//执行CGI响应
    {
      std::cout << "PATH: " << info._path_phys << std::endl;
      //将HTTP头信息和正文全部交给子进程处理
      //使用环境变量传递头信息
      //使用管道传递正文信息
      //使用管道接收CGI程序处理结果
      //创建管道、创建子进程、设置子进程环境变量setenv()、程序替换
      int in[2];//用于向子进程传递正文数据
      int out[2];//用于从子进程中读取处理结果
      if(pipe(in)||pipe(out)){
        info._err_code="500";
        ErrHandler(info);
        return false;
      }
      int pid=fork();
      if (pid < 0)
      {
        info._err_code = "500";
        ErrHandler(info);
        return false;
      }

      else if (pid == 0)
      {
        //设置环境变量setenv和putenv
        //第三个参数表示对于已经存在的环境是否要覆盖当前的数据
        setenv("METHOD", info._method.c_str(), 1);
        setenv("VERSION", info._version.c_str(), 1);
        setenv("PATH_INFO", info._path_info.c_str(), 1);
        setenv("QUERY_STRING", info._query_string.c_str(), 1);
        for (auto it = info._hdr_list.begin(); it != info._hdr_list.end(); it++)
        {
          //将所有的头信息都放到环境变量中
          setenv(it->first.c_str(), it->second.c_str(), 1);
          //std::cout << it->first.c_str() << "--" << it->second.c_str() << std::endl;
        }

        close(in[1]);//关闭写
        close(out[0]);//关闭读
        //子进程将从标准输入读取正文数据
        dup2(in[0], 0);
        //子进程直接打印处理结果传递给父进程
        //子进程将数据输出到标准输出
        dup2(out[1], 1);

        //进行程序替换，第一个参数表示要执行的文件的路径，第二个参数表示如何执行这个二进制程序
        //程序替换之后，原先该进程的数据全部都发生了改变
        execl(info._path_phys.c_str(), info._path_info.c_str(), NULL);
        exit(0);

      }
      close(in[0]);
      close(out[1]);
      //走下来就是父进程
      //1.通过in管道将正文数据传递给子进程
      auto it = info._hdr_list.find("Content-Length");
      //没有找到Content-Length,不需要提交正文数据给子进程
      //到这里就是http请求头中有着Content-Length这个字段,也就是说明需要父进程需要将bady数据传输给子进程
      if (it != info._hdr_list.end())
      {
        char buf[MAX_BUFF] = { 0 };
        int64_t content_len = Utils::StrToDigit(it->second);
        //循环读取正文，防止没有读完,直到读取正文大小等于Content-Length
        //tlen就是当前读取的长度
        int tlen = 0;
        while (tlen < content_len)
        {
          //防止粘包
          int len = MAX_BUFF > (content_len - tlen) ? (content_len - tlen) : MAX_BUFF;
          LOG("len:%d\n",len);
          LOG("content_len:%d\n",content_len);
          LOG("tlen:%d\n",tlen);
          int rlen = recv(_cli_sock, buf, len, 0);
          if (rlen <= 0)
          {
            //响应错误给客户端
            return false;
          }
          LOG("111\n");
          //子进程没有读取，直接写有可能管道满了，就会导致阻塞着
          if (write(in[1], buf, rlen) < 0)
          {
            return false;
          }
      LOG("222222\n");
          tlen += rlen;
        }
      }

      //2.通过out管道读取子进程的处理结果直到返回0
      //3.将处理结果组织http资源，响应给客户端
      std::string rsp_header;
      rsp_header = info._version + " 200 OK\r\n";
      rsp_header += "Content-Type: ";
      rsp_header += "text/html";
      rsp_header += "\r\n";
      rsp_header += "Connection: close\r\n";
      rsp_header += "ETag: " + _etag + "\r\n";
      //rsp_header += "Content-Length: " + _fsize + "\r\n";
      rsp_header += "Last-Modified: " + _mtime + "\r\n";
      rsp_header += "Date: " + _date + "\r\n\r\n";
      SendData(rsp_header);

      std::cout << "In ProcessCGI:rsp_header\n" << rsp_header << std::endl;

      while (1)
      {
        char buf[MAX_BUFF] = { 0 };
        int rlen = read(out[0], buf, MAX_BUFF);
        if (rlen == 0)
        {
          break;
        }
      LOG("1111\n");
        //读取子进程的处理结果并且发送给浏览器
        send(_cli_sock, buf, rlen, 0);
      LOG("111\n");
      }

      close(in[1]);
      close(out[0]);

      return true;
    }
    bool CGIHandler(RequestInfo &info)
    {
      InitResponse(info);//初始化CGI响应信息
      ProcessCGI(info);//执行CGI响应
      //将HTTP头信息和正文全部交给子进程处理
      //使用环境变量传递头信息
      //使用管道传递正文信息
      //使用管道接收CGI程序处理结果
      //创建管道、创建子进程、设置子进程环境变量setenv()、程序替换
      return true;
    }
    bool FileIsDir(RequestInfo& info)
    {
      if (info._st.st_mode & S_IFDIR)
      {
        //std::string path = info._path_info;
        //if (path[path.length() - 1] != '/')
        if (info._path_info.back() != '/')
        {
          info._path_info.push_back('/');
        }
        if (info._path_phys.back() != '/')
        {
          info._path_phys.push_back('/');
        }
        return true;
      }
      return false;
    }
    bool  FileHandler(RequestInfo &info)
    {
      LOG("FILEHANDLER\n");
      InitResponse(info);
      if(FileIsDir(info)){
        LOG("fileloist\n");
        ProcessList(info);//执行列表响应
      }
      else
        ProcessFile(info);//执行文件响应
      return true;
    }
};

