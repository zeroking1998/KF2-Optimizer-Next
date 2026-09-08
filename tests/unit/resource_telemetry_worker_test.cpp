#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "kf2/telemetry/resource_telemetry_worker.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

using namespace std::chrono_literals;
using kf2::telemetry::ResourceSampleBatch;
using kf2::telemetry::ResourceSampleGroup;
using kf2::telemetry::ResourceSampleRequest;
using kf2::telemetry::ResourceTelemetryBinding;
using kf2::telemetry::ResourceTelemetryWorker;

std::shared_ptr<const kf2::telemetry::ResourceTelemetrySnapshot>
wait_for_generation(ResourceTelemetryWorker& worker, std::uint64_t generation) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = worker.latest();
        if (snapshot && snapshot->generation == generation) return snapshot;
        std::this_thread::sleep_for(1ms);
    }
    return {};
}

ResourceTelemetryBinding binding(std::uint32_t pid,
                                 std::uint64_t process_start_id,
                                 std::uint64_t adapter_luid = 0) {
    ResourceTelemetryBinding result;
    result.identity = {pid, process_start_id};
    if (adapter_luid != 0) result.adapter_luid = adapter_luid;
    result.adapter_name = L"Test adapter";
    result.adapter_vendor_id = 0x10DE;
    return result;
}

}  // namespace

int main() {
    std::chrono::microseconds request_batch_elapsed{};
    {
        std::atomic<int> calls{0};
        std::atomic<int> priority{THREAD_PRIORITY_ERROR_RETURN};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                priority = GetThreadPriority(GetCurrentThread());
                ResourceSampleBatch batch;
                batch.group = request.group;
                if (request.group == ResourceSampleGroup::process_and_memory) {
                    kf2::telemetry::ProcessMetrics process;
                    process.private_bytes = request.binding.identity.pid;
                    batch.process = process;
                }
                ++calls;
                return batch;
            }};
        const auto generation = worker.bind(binding(41, 4100, 7));
        worker.request(1'000);
        const auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 41);
        CHECK(snapshot->identity.process_start_id == 4100);
        CHECK(snapshot->adapter_luid == 7);
        CHECK(snapshot->process);
        CHECK(snapshot->process->private_bytes == 41);
        CHECK(snapshot->process_sampled_at_ns == 1'000);
        CHECK(calls == 1);
        CHECK(priority == THREAD_PRIORITY_NORMAL);
    }

    // One worker serializes collection and coalesces repeated UI requests to
    // at most one pending sample while a sample is already running.
    {
        std::mutex mutex;
        std::condition_variable started;
        std::condition_variable release;
        bool first_started = false;
        bool allow_first_to_finish = false;
        std::atomic<int> active{0};
        std::atomic<int> maximum_active{0};
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token stop) {
                const int now_active = ++active;
                maximum_active = (std::max)(maximum_active.load(), now_active);
                const int call = ++calls;
                if (call == 1) {
                    std::unique_lock lock{mutex};
                    first_started = true;
                    started.notify_all();
                    release.wait(lock, [&] {
                        return allow_first_to_finish || stop.stop_requested();
                    });
                }
                --active;
                ResourceSampleBatch batch;
                batch.group = request.group;
                return batch;
            }};
        static_cast<void>(worker.bind(binding(42, 4200)));
        worker.request(2'000);
        {
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return first_started; }));
        }
        const auto before = std::chrono::steady_clock::now();
        for (std::uint64_t timestamp = 2'001; timestamp < 2'100; ++timestamp) {
            worker.request(timestamp);
        }
        request_batch_elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - before);
        CHECK(request_batch_elapsed < 50ms);
        {
            std::scoped_lock lock{mutex};
            allow_first_to_finish = true;
        }
        release.notify_all();
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (calls.load() < 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        CHECK(calls == 2);
        CHECK(maximum_active == 1);
    }

    // A completed result from an old process generation must never be
    // published after a restart or PID reuse.
    {
        std::mutex mutex;
        std::condition_variable started;
        std::condition_variable release;
        bool old_started = false;
        bool release_old = false;
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token stop) {
                if (request.binding.identity.process_start_id == 4300) {
                    std::unique_lock lock{mutex};
                    old_started = true;
                    started.notify_all();
                    release.wait(lock, [&] {
                        return release_old || stop.stop_requested();
                    });
                }
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = request.binding.identity.process_start_id;
                batch.process = process;
                return batch;
            }};
        const auto old_generation = worker.bind(binding(43, 4300));
        worker.request(3'000);
        {
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return old_started; }));
        }
        const auto new_generation = worker.bind(binding(44, 4400));
        CHECK(new_generation != old_generation);
        worker.request(4'000);
        {
            std::scoped_lock lock{mutex};
            release_old = true;
        }
        release.notify_all();
        const auto snapshot = wait_for_generation(worker, new_generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 44);
        CHECK(snapshot->identity.process_start_id == 4400);
        CHECK(snapshot->process);
        CHECK(snapshot->process->private_bytes == 4400);
    }

    // A map/session transition invalidates immutable samples without replacing
    // the single worker or losing its process binding.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = ++calls;
                batch.process = process;
                return batch;
            }};
        const auto first_generation = worker.bind(binding(49, 4900));
        worker.request(4'900);
        CHECK(wait_for_generation(worker, first_generation));
        const auto map_generation = worker.invalidate_samples();
        CHECK(map_generation != 0);
        CHECK(map_generation != first_generation);
        CHECK(!worker.latest());
        worker.request(4'901);
        const auto snapshot = wait_for_generation(worker, map_generation);
        CHECK(snapshot);
        CHECK(snapshot->identity.pid == 49);
        CHECK(snapshot->identity.process_start_id == 4900);
        CHECK(calls == 2);
    }

    // Clearing a session invalidates an in-flight result. A later binding can
    // recover normally without creating a second worker.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                ++calls;
                ResourceSampleBatch batch;
                batch.group = request.group;
                if (calls == 1) std::this_thread::sleep_for(20ms);
                kf2::telemetry::ProcessMetrics process;
                process.private_bytes = request.binding.identity.pid;
                batch.process = process;
                return batch;
            }};
        static_cast<void>(worker.bind(binding(45, 4500)));
        worker.request(5'000);
        worker.clear();
        std::this_thread::sleep_for(30ms);
        CHECK(!worker.latest());
        const auto generation = worker.bind(binding(46, 4600));
        worker.request(6'000);
        const auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot && snapshot->identity.pid == 46);
    }

    // A provider exception clears only that sample and the same worker
    // recovers on a later request.
    {
        std::atomic<int> calls{0};
        ResourceTelemetryWorker worker{
            [&](const ResourceSampleRequest& request, std::stop_token) {
                if (++calls == 1) throw std::runtime_error{"test failure"};
                ResourceSampleBatch batch;
                batch.group = request.group;
                kf2::telemetry::GpuMetrics gpu;
                gpu.adapter_gpu_percent = 54.0;
                batch.gpu = gpu;
                return batch;
            }};
        const auto generation = worker.bind(binding(48, 4800));
        worker.request(8'000);
        auto snapshot = wait_for_generation(worker, generation);
        CHECK(snapshot);
        CHECK(snapshot->process_sampled_at_ns == 8'000);
        CHECK(!snapshot->process);
        worker.request(8'100);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            snapshot = worker.latest();
            if (snapshot && snapshot->gpu_sampled_at_ns == 8'100) break;
            std::this_thread::sleep_for(1ms);
        }
        CHECK(snapshot && snapshot->gpu);
        CHECK(snapshot->gpu->adapter_gpu_percent == 54.0);
    }

    // Shutdown cancellation is cooperative and joins the only worker thread.
    {
        std::mutex mutex;
        std::condition_variable started;
        bool callback_started = false;
        const auto before = std::chrono::steady_clock::now();
        {
            ResourceTelemetryWorker worker{
                [&](const ResourceSampleRequest& request, std::stop_token stop) {
                    {
                        std::scoped_lock lock{mutex};
                        callback_started = true;
                    }
                    started.notify_all();
                    while (!stop.stop_requested()) {
                        std::this_thread::sleep_for(1ms);
                    }
                    ResourceSampleBatch batch;
                    batch.group = request.group;
                    return batch;
                }};
            static_cast<void>(worker.bind(binding(47, 4700)));
            worker.request(7'000);
            std::unique_lock lock{mutex};
            CHECK(started.wait_for(lock, 2s, [&] { return callback_started; }));
        }
        CHECK(std::chrono::steady_clock::now() - before < 2s);
    }

    // The production worker performs incremental Launch.log I/O away from the
    // caller and reports truncation as an explicit parser reset.
    {
        namespace fs = std::filesystem;
        const fs::path root{KF2_TEST_ROOT};
        fs::remove_all(root);
        fs::create_directories(root);
        const auto log = root / L"Launch.log";
        {
            std::ofstream output(log, std::ios::binary);
            output << "first line\n";
        }
        wchar_t module[MAX_PATH + 1]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        CHECK(length > 0 && length < MAX_PATH);
        const auto identity = kf2::game::bind_game_process(
            GetCurrentProcessId(), fs::path{module});
        CHECK(identity.has_value());
        ResourceTelemetryBinding log_binding;
        log_binding.identity = {
            identity.value().pid, identity.value().process_start_id};
        log_binding.game_log_directory = root;
        {
            ResourceTelemetryWorker worker;
            static_cast<void>(worker.bind(log_binding));
            worker.request(9'000);
            CHECK(worker.wait_until_idle(2s));
            auto chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().reset_parser);
            CHECK(chunks.front().bytes == "first line\n");

            {
                std::ofstream output(log, std::ios::binary | std::ios::app);
                output << "second line\n";
            }
            worker.request(9'100);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(!chunks.front().reset_parser);
            CHECK(chunks.front().bytes == "second line\n");

            {
                std::ofstream output(log, std::ios::binary | std::ios::trunc);
                output << "new\n";
            }
            worker.request(9'200);
            CHECK(worker.wait_until_idle(2s));
            chunks = worker.take_game_log_chunks(log_binding.identity);
            CHECK(chunks.size() == 1);
            CHECK(chunks.front().reset_parser);
            CHECK(chunks.front().bytes == "new\n");
        }
        fs::remove_all(root);
    }

    std::cout << "nonblocking_request_batch_us="
              << request_batch_elapsed.count() << '\n';
    return EXIT_SUCCESS;
}
