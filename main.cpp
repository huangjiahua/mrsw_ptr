#include <iostream>
#include <thread>
#include <vector>
#include "mrsw_ptr_trail.h"
#include "mrsw_ptr.h"

using namespace std;

const size_t kTasks = 10000;

atomic<bool> flag(false);

void writeWorker(MrswPtr<long> &ptr, vector<long> &log) {
    while (!flag.load());
    for (size_t i = 0; i < kTasks; i++) {
        auto writer = ptr.GetWriter();
        log[i] = *writer;
        long *k = writer.Swap(new long(log[i] + 1));
        assert(*k == log[i]);
        delete k;
    }
}

void readWorker(MrswPtr<long> &ptr, vector<long> &log) {
    while (!flag.load());
    for (size_t i = 0; i < kTasks; i++) {
        auto reader = ptr.GetReader();
        log[i] = *reader;
    }
}

int main() {
    vector<vector<long>> record(4, vector<long>(kTasks, -1));
    vector<thread> threads(4);
    MrswPtr<long> mp(new long(0));
    for (int i = 0; i < 2; i++) {
        threads[i] = thread(writeWorker, std::ref(mp), std::ref(record[i]));
    }
    for (int i = 2; i < 4; i++) {
        threads[i] = thread(readWorker, std::ref(mp), std::ref(record[i]));
    }

    flag.store(true); // launch all workers

    for (auto &t : threads) t.join();

    for (size_t i = 0; i < kTasks; i++) {
        for (auto &v : record) {
            cout << v[i] << "\t";
        }
        cout << endl;
    }
    return 0;
}