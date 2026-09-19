#pragma once
// Tiny threading glue: blocking work runs on a detached thread, results come back on the main thread via MainQueue.
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ss {

class MainQueue {
public:
  void post(std::function<void()> fn) { std::lock_guard<std::mutex> g(m_); q_.push_back(std::move(fn)); }
  /** Runs everything posted so far (on the main thread). */
  void drain() {
    std::vector<std::function<void()>> now;
    { std::lock_guard<std::mutex> g(m_); now.swap(q_); }
    for (auto& f : now) f();
  }
private:
  std::mutex m_; std::vector<std::function<void()>> q_;
};

/** Run `work` off-thread; on success `ok(result)` runs on the main thread, on exception `fail(message)` does. */
template <class T>
void async_call(MainQueue& mq, std::function<T()> work, std::function<void(T)> ok, std::function<void(std::string)> fail) {
  std::thread([&mq, work = std::move(work), ok = std::move(ok), fail = std::move(fail)]() {
    try {
      T r = work();
      mq.post([ok, r = std::move(r)]() mutable { ok(std::move(r)); });
    } catch (const std::exception& e) {
      std::string msg = e.what();
      mq.post([fail, msg]() { fail(msg); });
    }
  }).detach();
}

} // namespace ss
