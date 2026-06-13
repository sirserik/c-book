// Бенчмарк мини-СУБД. Замеряет:
//   - вставка N строк (через SQL)
//   - checkpoint время
//   - find по primary key (id)
//   - find по value БЕЗ secondary index (full scan)
//   - find по value С secondary index
//
// Использование: ./build/bench [N]  (по умолчанию 1000)

#include "database.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

using clk = std::chrono::steady_clock;

static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char* argv[]) {
    int N = (argc > 1) ? std::atoi(argv[1]) : 1000;
    if (N <= 0) N = 1000;

    std::cout << "=== Бенчмарк мини-СУБД, N = " << N << " ===\n\n";
    std::cout << "Note: MAX_KEYS=4 в нашем B+tree (учебное), реальная СУБД 200+\n\n";

    std::system("rm -rf data/bench");
    ::mkdir("data", 0755);

    db::Database db("data/bench");
    std::ostringstream sink;

    db.execute("CREATE TABLE items (id INT PRIMARY KEY, value INT);", sink);

    // 1) Вставки
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "INSERT INTO items VALUES (%d, %d);", i, i * 10);
        sink.str("");
        db.execute(buf, sink);
    }
    auto t1 = clk::now();
    double t_insert = ms(t0, t1);
    std::cout << "Insert:                " << t_insert << " ms, "
              << (1000.0 * N / t_insert) << " ops/s\n";

    // 2) Checkpoint
    auto t2 = clk::now();
    db.checkpoint();
    auto t3 = clk::now();
    std::cout << "Checkpoint:            " << ms(t2, t3) << " ms\n\n";

    // 3) Find by id (primary key — O(log N))
    t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "SELECT * FROM items WHERE id = %d;", i);
        sink.str("");
        db.execute(buf, sink);
    }
    t1 = clk::now();
    double t_find_id = ms(t0, t1);
    std::cout << "Find by id (B+tree):   " << t_find_id << " ms, "
              << (1000.0 * N / t_find_id) << " ops/s\n";

    // 4) Find by value БЕЗ индекса. Каждый запрос — full scan.
    int sample = std::min(100, N);
    t0 = clk::now();
    for (int i = 0; i < sample; ++i) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "SELECT * FROM items WHERE value = %d;", i * 10);
        sink.str("");
        db.execute(buf, sink);
    }
    t1 = clk::now();
    double t_scan_value = ms(t0, t1);
    std::cout << "Find by value (full scan, " << sample << " samples): "
              << t_scan_value << " ms, "
              << (1000.0 * sample / t_scan_value) << " ops/s\n";

    // 5) Создаём secondary index
    auto ti0 = clk::now();
    db.execute("CREATE INDEX idx ON items(value);", sink);
    auto ti1 = clk::now();
    std::cout << "Build index:           " << ms(ti0, ti1) << " ms\n";

    // 6) Find by value С индексом
    t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "SELECT * FROM items WHERE value = %d;", i * 10);
        sink.str("");
        db.execute(buf, sink);
    }
    t1 = clk::now();
    double t_find_value = ms(t0, t1);
    std::cout << "Find by value (index): " << t_find_value << " ms, "
              << (1000.0 * N / t_find_value) << " ops/s\n\n";

    std::cout << "Ускорение find-by-value с индексом: "
              << ((t_scan_value / sample) / (t_find_value / N)) << "x\n";

    return 0;
}
