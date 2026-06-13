// Демо: race condition на простом ++ и три способа его исправить —
// mutex, atomic с дефолтным seq_cst, atomic с relaxed.

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

constexpr int ITER = 1000000;

using clk = std::chrono::steady_clock;
double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

int main() {
    // 1) Без защиты — race condition.
    {
        int counter = 0;
        auto t0 = clk::now();
        std::thread a([&] { for (int i = 0; i < ITER; ++i) ++counter; });
        std::thread b([&] { for (int i = 0; i < ITER; ++i) ++counter; });
        a.join(); b.join();
        auto t1 = clk::now();
        std::cout << "Без защиты:        counter=" << counter
                  << " (ожидалось " << (2 * ITER) << "), "
                  << ms(t0, t1) << " ms\n";
    }

    // 2) std::mutex + ++.
    {
        int counter = 0;
        std::mutex m;
        auto t0 = clk::now();
        auto work = [&] {
            for (int i = 0; i < ITER; ++i) {
                std::lock_guard<std::mutex> lock(m);
                ++counter;
            }
        };
        std::thread a(work), b(work);
        a.join(); b.join();
        auto t1 = clk::now();
        std::cout << "Mutex:             counter=" << counter
                  << ", " << ms(t0, t1) << " ms\n";
    }

    // 3) atomic, дефолтный seq_cst.
    {
        std::atomic<int> counter{0};
        auto t0 = clk::now();
        std::thread a([&] { for (int i = 0; i < ITER; ++i) ++counter; });
        std::thread b([&] { for (int i = 0; i < ITER; ++i) ++counter; });
        a.join(); b.join();
        auto t1 = clk::now();
        std::cout << "atomic (seq_cst):  counter=" << counter.load()
                  << ", " << ms(t0, t1) << " ms\n";
    }

    // 4) atomic с relaxed — самый быстрый, но без synchronization-with.
    {
        std::atomic<int> counter{0};
        auto t0 = clk::now();
        std::thread a([&] {
            for (int i = 0; i < ITER; ++i) counter.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread b([&] {
            for (int i = 0; i < ITER; ++i) counter.fetch_add(1, std::memory_order_relaxed);
        });
        a.join(); b.join();
        auto t1 = clk::now();
        std::cout << "atomic (relaxed):  counter=" << counter.load()
                  << ", " << ms(t0, t1) << " ms\n";
    }

    return 0;
}
