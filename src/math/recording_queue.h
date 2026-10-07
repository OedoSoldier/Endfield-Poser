#pragma once
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace recording_queue {
struct Work {
  std::function<void()> run,rollback;
  uint32_t commits=0;
  // prepare touches private CPU buffers only; run/rollback own all file IO.
  std::function<void()> prepare;
  size_t bytes=0;
};
struct Statistics {
  uint64_t prepared=0,written=0,blocked=0;
  size_t peakItems=0,peakBytes=0;
  double prepareMs=0,writeMs=0;
};
// Bounded parallel preparation, followed by one ordered writer. The limit
// includes encoding, ready and writing jobs. Only the writer stages/commits or
// rolls back files, even if encoders finish out of order or one of them fails.
class Writer {
  struct Entry {Work work;bool started=false,ready=false;std::exception_ptr error;};
  const size_t capacity,byteLimit;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::shared_ptr<Entry>> queue;
  size_t count=0,bytes=0,preparing=0;
  bool closing=false;
  std::exception_ptr failure;
  std::atomic<uint32_t> saved{0};
  Statistics stats;
  std::vector<std::thread> encoders;
  std::thread worker;
  using Clock=std::chrono::steady_clock;
  static double Millis(Clock::time_point since){return std::chrono::duration<double,std::milli>(Clock::now()-since).count();}
  bool unstarted() const {for(auto &e:queue)if(!e->started)return true;return false;}
  void encode() {
    for(;;) {
      std::shared_ptr<Entry> item;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,[&]{return closing||failure||unstarted();});
        if(failure)return;
        for(auto &e:queue)if(!e->started){item=e;e->started=true;++preparing;break;}
        if(!item)return;
      }
      const auto started=Clock::now();std::exception_ptr error;
      try {item->work.prepare();}catch(...){error=std::current_exception();}
      item->work.prepare={};
      {
        std::lock_guard<std::mutex> lock(mutex);
        item->error=error;item->ready=true;--preparing;
        ++stats.prepared;stats.prepareMs+=Millis(started);
      }
      wake.notify_all();
    }
  }
  void execute() {
    for(;;) {
      std::shared_ptr<Entry> item;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,[&]{return (!queue.empty()&&queue.front()->ready)||(closing&&queue.empty());});
        if(queue.empty())return;
        item=queue.front();
      }
      const auto started=Clock::now();
      try {
        if(item->error)std::rethrow_exception(item->error);
        item->work.run();
        if(item->work.commits)saved.store(item->work.commits,std::memory_order_release);
      } catch(...) {
        const auto error=std::current_exception();
        std::deque<std::shared_ptr<Entry>> abandoned;
        {
          std::unique_lock<std::mutex> lock(mutex);
          failure=error;closing=true;wake.notify_all();
          // Let active encoders release their buffers before reporting drained.
          wake.wait(lock,[&]{return preparing==0;});abandoned.swap(queue);
        }
        for(auto &e:abandoned)if(e->work.rollback){try{e->work.rollback();}catch(...){}}
        abandoned.clear();item.reset();
        std::lock_guard<std::mutex> lock(mutex);count=0;bytes=0;return;
      }
      const auto reserved=item->work.bytes;item->work={};
      std::lock_guard<std::mutex> lock(mutex);queue.pop_front();--count;bytes-=reserved;
      ++stats.written;stats.writeMs+=Millis(started);
    }
  }
public:
  explicit Writer(size_t limit=4,size_t parallel=2,size_t budget=512ull*1024*1024):capacity(limit?limit:1),byteLimit(budget) {
    try {
      for(size_t i=0;i<(parallel?parallel:1);++i)encoders.emplace_back([this]{encode();});
      worker=std::thread([this]{execute();});
    } catch(...) {
      {std::lock_guard<std::mutex> lock(mutex);closing=true;}
      wake.notify_all();for(auto &e:encoders)if(e.joinable())e.join();throw;
    }
  }
  Writer(const Writer&)=delete;
  Writer &operator=(const Writer&)=delete;
  ~Writer() {
    {std::lock_guard<std::mutex> lock(mutex);closing=true;}
    wake.notify_all();for(auto &e:encoders)if(e.joinable())e.join();if(worker.joinable())worker.join();
  }
  bool submit(Work &work) {
    std::lock_guard<std::mutex> lock(mutex);
    if(failure)std::rethrow_exception(failure);
    if(closing)return false;
    // One oversized image may proceed alone; otherwise enforce both bounds.
    if(count==capacity||(count&&byteLimit&&(bytes>=byteLimit||work.bytes>byteLimit-bytes))){++stats.blocked;return false;}
    auto item=std::make_shared<Entry>();queue.push_back(item);
    item->work=std::move(work);work={};item->ready=item->started=!bool(item->work.prepare);
    ++count;bytes+=item->work.bytes;
    if(count>stats.peakItems)stats.peakItems=count;if(bytes>stats.peakBytes)stats.peakBytes=bytes;
    wake.notify_all();return true;
  }
  size_t pending() const {std::lock_guard<std::mutex> lock(mutex);return count;}
  uint32_t completed() const {return saved.load(std::memory_order_acquire);}
  Statistics statistics() const {std::lock_guard<std::mutex> lock(mutex);return stats;}
  void check() const {std::lock_guard<std::mutex> lock(mutex);if(failure)std::rethrow_exception(failure);}
};
}
