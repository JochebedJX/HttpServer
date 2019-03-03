#include"utils.hpp"


enum _boundary_type{
  BOUNDARY_NO=0,
  BOUNDARY_FIRST,
  BOUNDARY_MIDDLE,
  BOUNDARY_LAST,
  BOUNDARY_BAK
};

class UpLoad
{
  //CGI外部程序中的文件上传功能处理接口
  private:
    int _file_fd;
    int64_t content_len;
    string _file_name;
    string _first_boundary;
    string _middle_boundary;
    string _last_boundary;

  private:
    int MatchBoundary(char * buf,int blen,int *boundary_pos){
      //----boundary 
      //first_boundary:   ------boundary 
      //middle_boundary:  \r\n------boundary\r\n 
      //last_boundary:    \r\n------boundary--
      //middle_boundary和last_boundary的长度是一样的

      if(!memcmp(buf,_first_boundary.c_str(),_first_boundary.length())){//从起始位置匹配first_boundary
        *boundary_pos = 0;
        return BOUNDARY_FIRST;
      }
      for(int i=0;i<blen;i++)
      {
        //字符串剩余长度大于boundary的长度，则全部匹配
        if((blen-i)>=_middle_boundary.length()){
          if(!memcmp(buf+i,_middle_boundary.c_str(),_middle_boundary.length()))
          {
            *boundary_pos = i;
            return BOUNDARY_MIDDLE;
          }
          if(!memcmp(buf+i,_last_boundary.c_str(),_last_boundary.length()))
            *boundary_pos = i;
          return BOUNDARY_LAST;
        }
        else 
        {
          //剩余长度小于boundary长度，防止出现半个boundary，所以进行部分匹配
          int cmp_len=(blen-i);
          if(!memcmp(buf+i,_middle_boundary.c_str(),cmp_len)){
            *boundary_pos = i;
            return BOUNDARY_BAK;
          }
          if(!memcmp(buf+i,_last_boundary.c_str(),cmp_len)){
            *boundary_pos = i;
            return BOUNDARY_BAK;
          }
        }
      }
      return BOUNDARY_NO;
    }

    bool GetFileName(char *buf,int * content_pos)
    {
      char* ptr=NULL;
      ptr=strstr(buf,"\r\n\r\n");
      if(ptr==NULL)
      {
        *content_pos=0;
        return false;
      }
      *content_pos=ptr-buf+4;
      string header;
      header.assign(buf,ptr-buf);
      size_t pos=header.find("filename=\"");
      if(pos==string::npos)
      {
        return false;
      }
      std::string file_name;
      std::string file_sep="filename=\"";
      file_name=header.substr(pos+file_sep.length());
      pos=file_name.find("\"");
      if(pos==string::npos)
      {
        return false;
      }
      file_name.erase(pos);

      //如果直接使用WWWROOT进行拼接获取文件所在路径和名字的时候这个时候就会每次上传的文件都在www目录下，不会发生改变
      //所以要使用实际路径在加上文件名就好了
      _file_name = WWWROOT;
      _file_name += "/";
      _file_name += file_name;
      fprintf(stderr,"upload file:[%s]\n",_file_name.c_str());

      return true;
    }

    bool CreateFile()
    {
      _file_fd=open(_file_name.c_str(),O_CREAT|O_WRONLY,0664);
      if(_file_fd<0)
      {
        fprintf(stderr,"open error:%s\n",strerror(errno));
        return false;
      }
      return true;
    }

    bool CloseFile()
    {
      if(_file_fd!=-1)
      {
        close(_file_fd);
        _file_fd=-1;
      }
      return true;
    }

    bool WriteFile(char* buf,int len)
    {
      if(_file_fd!=-1)
      {
        write(_file_fd,buf,len);
      }
      return true;
    }

  public:
    UpLoad():_file_fd(-1){}

    bool InitUpLoadInfo()//初始化上传信息，正文长度，boundary
    {
      umask(0);
      char* ptr=getenv("Content-Length");
      if(ptr==NULL)
      {
        fprintf(stderr,"have no Content-Length!!!\n");
        return false;
      }
      content_len=Utils::StrToDigit(ptr);

      ptr=getenv("Content-Type");
      if(ptr==NULL)
      {
        fprintf(stderr,"have no Content-Length!!!\n");
        return false;
      }

      string boundary_sep="boundary=";
      string content_type=ptr;
      size_t pos=content_type.find(boundary_sep);
      if(pos==string::npos)
      {
        fprintf(stderr,"contenmt_type have no Content-Length!!!\n");
        return false;
      }
      string boundary;
      boundary=content_type.substr(pos+boundary_sep.length());
      _first_boundary="--"+boundary;
      _middle_boundary="\r\n"+boundary+"\r\n";
      _last_boundary="\r\n"+boundary+"--";

      return true;

    }
    bool ProcessUpLoad()//对正文进行处理，将文件数据进行存储，完成文件的上传存储功能。1.从正文起始位置匹配_first_boundry、2.从boundry头部信息获取上传文件名称、3.继续循环从剩下的正文中匹配_middle_boundry，_middle_boundry之前的数据是上一个文件数据，将之前的数据存储到文件中，关闭文件，从头部信息中获取文件名，若有，打开文件、4.当匹配到_last_boundry的时候，将boundry之前的数据存储到文件，文件上传处理完毕。
    {
      //tlen : 当前已经读取的长度，相当于标志位
      //blen : buffer长度
      int64_t tlen=0,blen=0;
      char buf[MAX_BUFF];
      while(tlen<content_len)
      {
        int len=read(0,buf+blen,MAX_BUFF-blen);
        blen+=len;
        int boundary_pos;
        int content_pos;
        if(MatchBoundary(buf,blen,&boundary_pos)==BOUNDARY_FIRST)
        {//从boundary头中获取文件名
          //若获取文件名成功，则创文件，打开文件
          //将头部信息从buf中移出，剩下数据下一步匹配
          if(GetFileName(buf,&content_pos))
          {
            CreateFile();

            blen-=content_pos;
            memmove(buf,buf+content_pos,blen);
            memset(buf + blen, 0, content_pos);
          }

          //有可能不是上传文件，没有filename所以匹配到了_f_boundary也要将其去掉
          //没有匹配成功就把boundary分隔符的内容去除，因为此时的content_pos的位置没有找到呢
          blen -= boundary_pos;
          memmove(buf, buf + boundary_pos, blen);
          memset(buf + blen, 0, boundary_pos);
          fprintf(stderr,  "[In BOUNDRY_FIRST只是去除分隔符->buf: %s]" , buf);
        }
        while(1){
          if(MatchBoundary(buf,blen,&boundary_pos)!=BOUNDARY_MIDDLE)
          {
            break;
          }
          //将boundary之前的数据写入文件，将数据从buf一处
          //看boundary头中是否有文件名
          WriteFile(buf,boundary_pos);
          CloseFile();
          blen-=(boundary_pos);
          memmove(buf,buf+content_pos,blen);
          memset(buf+blen,0,boundary_pos);
          if(GetFileName(buf,&content_pos))
          {
            CreateFile();
            blen-=content_pos;
            memmove(buf,buf+content_pos,blen);
            memset(buf+blen,0,content_pos);
          }
          else 
          {
            //此时遇到的这个middle分隔符，后面的数据不是为了上传文件
            //头信息不全跳出循环,没找到\r\n\r\n，等待再次从缓存区中拿取数据，再次循环进来进行判断
            if (content_pos == 0)
            {
              break;
            }
            //没有找到名字或者名字后面的
            //没有匹配成功就把boundary去除,防止下次进入再找这一个boundary
            blen -= _middle_boundary.length();
            memmove(buf, buf + _middle_boundary.length(), blen);
            memset(buf + blen, 0, _middle_boundary.length());
          }
        }
        if(MatchBoundary(buf,blen,&boundary_pos)==BOUNDARY_LAST)
        {
          WriteFile(buf,boundary_pos);
          CloseFile();
          return true;
        }
        if(MatchBoundary(buf,blen,&boundary_pos)==BOUNDARY_BAK)
        {
          //将类似boundary位置之前的数据写入文件
          //一处之前的数据
          //剩下的数据不懂，重新继续接收数据，补全匹配

          WriteFile(buf,boundary_pos);
          blen-=(boundary_pos);
          memmove(buf,buf+content_pos,blen);
          memset(buf + blen, 0, boundary_pos);
          continue;
        }
        if(MatchBoundary(buf,blen,&boundary_pos)==BOUNDARY_NO)
        {
          WriteFile(buf,blen);
          blen=0;
        }
        tlen+=len;
      }
      return true;
    }
};

int main()
{
  //要将管道中的数据都读取完毕，这个时候，父进程才可以将html页面中的数据发送给浏览器
  UpLoad upload;
  std::string rsp_body;
  if (upload.InitUpLoadInfo() == false)
  {
    return 0;
  }
  if (upload.ProcessUpLoad() == false)
  {
    rsp_body = "<html><body><h1>FALSE</h1></body></html>";
  }
  else
  {
    rsp_body = "<html><body><h1>SUCCESS</h1></body></html>";
  }
  //将数据写到标准输出，就会写到管道中
  std::cout << rsp_body;
  fflush(stdout);
  return 0;
}
