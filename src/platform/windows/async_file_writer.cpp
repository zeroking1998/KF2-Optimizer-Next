#include "kf2/platform/windows/async_file_writer.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::platform::windows {
namespace {
#ifdef KF2_ASYNC_FILE_WRITER_TESTING
std::atomic_bool fail_next_outcome_publication{false};
#endif
}

#ifdef KF2_ASYNC_FILE_WRITER_TESTING
void detail::fail_next_async_file_outcome_publication() noexcept {
    fail_next_outcome_publication.store(true, std::memory_order_release);
}
#endif

class AsyncFileWriter::Impl final {
public:
    Impl() : thread_{[this](std::stop_token stop) { run(stop); }} {}

    ~Impl() {
        {
            std::scoped_lock lock{mutex_};
            stopping_ = true;
            thread_.request_stop();
            changed_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }

    std::uint64_t submit(std::filesystem::path target, std::string bytes) {
        if (target.empty() || bytes.empty()) return 0;
        std::scoped_lock lock{mutex_};
        if (stopping_ || worker_failed_) return 0;
        const auto ticket = ++next_ticket_;
        const auto pending = std::find_if(
            queue_.begin(), queue_.end(), [&](const Task& task) {
                return task.target == target;
            });
        if (pending == queue_.end()) {
            Task task;
            task.tickets.push_back(ticket);
            task.target = std::move(target);
            task.bytes = std::move(bytes);
            queue_.push_back(std::move(task));
        } else {
            pending->tickets.push_back(ticket);
            pending->bytes = std::move(bytes);
        }
        changed_.notify_one();
        return ticket;
    }

    bool wait(std::uint64_t ticket, std::chrono::milliseconds timeout) {
        if (ticket == 0) return false;
        std::unique_lock lock{mutex_};
        const auto completed = changed_.wait_for(lock, timeout, [&] {
            if (std::any_of(
                outcomes_.begin(), outcomes_.end(), [&](const Outcome& item) {
                    return item.ticket == ticket;
                })) return true;
            if (worker_failed_) return true;
            if (active_ || ticket > next_ticket_) return false;
            return std::none_of(queue_.begin(), queue_.end(),
                [&](const Task& task) {
                    return std::find(task.tickets.begin(), task.tickets.end(),
                                     ticket) != task.tickets.end();
                });
        });
        if (!completed) return false;
        const auto outcome = std::find_if(
            outcomes_.begin(), outcomes_.end(), [&](const Outcome& item) {
                return item.ticket == ticket;
            });
        return outcome != outcomes_.end() && outcome->succeeded;
    }

    bool wait_until_idle(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [this] {
            return worker_failed_ || (queue_.empty() && !active_);
        });
    }

private:
    struct Task {
        std::vector<std::uint64_t> tickets;
        std::filesystem::path target;
        std::string bytes;
    };
    struct Outcome {
        std::uint64_t ticket{0};
        bool succeeded{false};
    };

    void finish_failed_task() noexcept {
        try {
            std::scoped_lock lock{mutex_};
            active_ = false;
        } catch (...) {
        }
        changed_.notify_all();
    }

    void run(std::stop_token stop) noexcept {
        try {
            run_loop(stop);
        } catch (...) {
            try {
                std::scoped_lock lock{mutex_};
                worker_failed_ = true;
                active_ = false;
                queue_.clear();
            } catch (...) {
            }
            changed_.notify_all();
        }
    }

    void run_loop(std::stop_token stop) {
        static_cast<void>(SetThreadPriority(
            GetCurrentThread(), THREAD_PRIORITY_NORMAL));
        for (;;) {
            Task task;
            {
                std::unique_lock lock{mutex_};
                changed_.wait(lock, [&] {
                    return !queue_.empty() || stop.stop_requested();
                });
                if (queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop_front();
                active_ = true;
            }

            bool succeeded = false;
            try {
                const auto result = atomic_replace_utf8(
                    task.target, task.bytes);
                succeeded = result.has_value() && result.value();
            } catch (...) {
                succeeded = false;
            }

            try {
#ifdef KF2_ASYNC_FILE_WRITER_TESTING
                if (fail_next_outcome_publication.exchange(
                        false, std::memory_order_acq_rel)) {
                    throw std::bad_alloc{};
                }
#endif
                std::scoped_lock lock{mutex_};
                for (const auto ticket : task.tickets) {
                    outcomes_.push_back({ticket, succeeded});
                }
                while (outcomes_.size() > 256) outcomes_.pop_front();
                active_ = false;
            } catch (...) {
                finish_failed_task();
            }
            changed_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Task> queue_;
    std::deque<Outcome> outcomes_;
    std::uint64_t next_ticket_{0};
    bool active_{false};
    bool stopping_{false};
    bool worker_failed_{false};
    std::jthread thread_;
};

AsyncFileWriter::AsyncFileWriter()
    : implementation_{std::make_unique<Impl>()} {}

AsyncFileWriter::~AsyncFileWriter() = default;

std::uint64_t AsyncFileWriter::submit(
    std::filesystem::path target, std::string bytes) {
    return implementation_->submit(std::move(target), std::move(bytes));
}

bool AsyncFileWriter::wait(
    std::uint64_t ticket, std::chrono::milliseconds timeout) {
    return implementation_->wait(ticket, timeout);
}

bool AsyncFileWriter::wait_until_idle(std::chrono::milliseconds timeout) {
    return implementation_->wait_until_idle(timeout);
}

}  // namespace kf2::platform::windows
