#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "SafeQueue.h"

// 线程池实现
class ThreadPool
{
private:
  // 私有类，封装工作线程，使用std::function存储和调用函数
  class ThreadWorker
  {
  private:
    // thread Id
    int m_id;
    // 指向该线程所属线程池
    ThreadPool *m_pool;

  public:
    ThreadWorker(ThreadPool *pool, const int id)
        : m_pool(pool), m_id(id)
    {
    }
    // 重载opreator()
    void operator()()
    {
      std::function<void()> func;
      bool dequeued;
      // 只要线程池没有关闭，工作线程就会继续运行
      while (!m_pool->m_shutdown)
      {
        {
          // 安全访问
          std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);
          if (m_pool->m_queue.empty())
          {
            // 等待，等待期间锁被释放
            m_pool->m_conditional_lock.wait(lock);
          }
          // 从任务队列中取一个任务存入func
          dequeued = m_pool->m_queue.dequeue(func);
        }
        if (dequeued)
        {
          func();
        }
      }
    }
  };

  bool m_shutdown;
  SafeQueue<std::function<void()>> m_queue;
  std::vector<std::thread> m_threads;
  std::mutex m_conditional_mutex;
  std::condition_variable m_conditional_lock;

public:
  ThreadPool(const int n_threads)
      : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false)
  {
  }

  // Noncopyable & Nonmoveable
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;

  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  // 线程池初始化，为每个线程创建一个ThreadWorker实例，并启动线程
  void init()
  {
    for (int i = 0; i < m_threads.size(); ++i)
    {
      m_threads[i] = std::thread(ThreadWorker(this, i));
    }
  }

  // 一次通知所有等待线程，调用join，当所有线程结束任务后退出
  void shutdown()
  {
    m_shutdown = true;
    m_conditional_lock.notify_all();

    for (int i = 0; i < m_threads.size(); ++i)
    {
      if (m_threads[i].joinable())
      {
        m_threads[i].join();
      }
    }
  }

  // 模板方法，允许用户提交要异步执行的任务
  template <typename F, typename... Args>
  auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
  {
    // 封装任务并使用std::bind()绑定任务和参数
    std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    // Encapsulate it into a shared ptr in order to be able to copy construct / assign
    // 用shared_ptr包裹，以便拷贝构造和复制
    auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

    // 包装任务并入队
    std::function<void()> wrapper_func = [task_ptr]()
    {
      (*task_ptr)();
    };

    m_queue.enqueue(wrapper_func);

    // 唤醒一个线程，使其可以处理新入队的任务
    m_conditional_lock.notify_one();

    // 获取一个std::future对象并返回。这个std::future对象允许在未来某个时刻检索任务的执行结果
    return task_ptr->get_future();
  }
};

#endif