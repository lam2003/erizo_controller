#ifndef ERIZO_SRC_ERIZO_THREAD_THREADPOOL_H_
#define ERIZO_SRC_ERIZO_THREAD_THREADPOOL_H_

#include <memory>
#include <vector>

#include "worker.h"
#include "scheduler.h"

namespace erizo {

class ThreadPool {
 public:
  explicit ThreadPool(unsigned int num_workers);
  ~ThreadPool();

  std::shared_ptr<Worker> getLessUsedWorker();
  std::shared_ptr<Worker> getSequenceWorker();
  void start();
  void close();

 private:
  uint32_t index_;
  std::vector<std::shared_ptr<Worker>> workers_;
  std::shared_ptr<Scheduler> scheduler_;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_THREAD_THREADPOOL_H_
