#pragma once

#include<iostream>
#include<queue>
#include<pthread.h>

using namespace std;
#define MAX_THREAD 3

typedef bool (*Handler)(int sock);

class HttpTask
{
  //HTTP请求处理任务
  //包含一个成员socket
  //任务处理函数
  private:
    int _cli_sock;
	  Handler TaskHandler;
  public:
  	void SetHttpTask(int sock,Handler handle)
  	{
  		_cli_sock = sock;
  		TaskHandler = handle;
  	}
  	void handler()
  	{
  		TaskHandler(_cli_sock);
  	}
};

class ThreadPool
{
//线程池类
//创建指定数量的线程
//线程安全的任务队列
//提供任务的入队，出队，销毁/初始化接口
private:
	int _max_thr;//线程的最大数量
	int _cur_thr;//当前线程池中最大线程数
	queue<HttpTask> _task_queue;
	pthread_mutex_t  _mutex;
	pthread_cond_t  _cond;
  bool is_stop;

private:
	static void *thr_start(void* arg)//完成线程获取任务
  {
    ThreadPool* tp=(ThreadPool*)arg;

    pthread_detach(pthread_self());

    while(1)
    {
      tp->LockQueue();
      while(tp->Empty())
        tp->ThreadWait();
      HttpTask t=tp->PopTask();
      tp->UnlockQueue();
      t.handler();
    }
  }
public:
	ThreadPool(int max):_max_thr(max),_cur_thr(max),is_stop(false)
	{}
	bool ThreadPoolInit()//线程创建，互斥锁/条件变量初始化
	{
		pthread_mutex_init(&_mutex,NULL);
    pthread_cond_init(&_cond,NULL);

    for(size_t i=0;i<MAX_THREAD;i++)
    {
      pthread_t pid;
      pthread_create(&pid, NULL, thr_start, (void*)this); 
    }
    return true;
	}
  void LockQueue()
  {
    pthread_mutex_lock(&_mutex);
  }
  bool Empty()
  {
    return _task_queue.size()==0;
  }
  void ThreadWait()
  {
    if(is_stop)
    {
      UnlockQueue();
      _cur_thr--;
      pthread_exit((void*)0);
      
      cout << "thread:" << pthread_self() << "quit!" << endl;
      return;
    }
    pthread_cond_wait(&_cond,&_mutex);
  }
  void UnlockQueue()
  {
    pthread_mutex_unlock(&_mutex);
  }
  void NotifyOneThread()//实现同步
  {
    pthread_cond_signal(&_cond);
  }
  void NotifyAllThread()//唤醒所有线程
  {
    pthread_cond_broadcast(&_cond);
  }
	bool PushTask(HttpTask &tt)//入队
  {
    LockQueue();
    if(is_stop)
    {
      UnlockQueue();
      return false;
    }
    _task_queue.push(tt);
    NotifyOneThread();//实现同步
    UnlockQueue();
    return true;
  }
	HttpTask PopTask()//出队
  {
    HttpTask tt = _task_queue.front();
    _task_queue.pop();
    return tt;
  }
	bool ThreadPoolStop()//销毁线程池
  {
    LockQueue();
    is_stop = true;
    UnlockQueue();
    //线程的数量如果大于0说明还存在线程在阻塞唤醒所有线程并停止
    if(_cur_thr > 0)
    {
      NotifyAllThread();
    }
    return true;
  }
  ~ThreadPool()
  {
    pthread_mutex_destroy(&_mutex);
    pthread_cond_destroy(&_cond);
  }
};
