#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

struct CpuTimes {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
};

CpuTimes getCpuTimes() {
    std::ifstream file("/proc/stat");
    CpuTimes times{};
    std::string line;
    if (file.is_open()) {
        std::getline(file, line);
        sscanf(line.c_str(), "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
               &times.user, &times.nice, &times.system, &times.idle,
               &times.iowait, &times.irq, &times.softirq, &times.steal);
    }
    return times;
}

double calculateCpuUsage(const CpuTimes& prev, const CpuTimes& curr) {
    unsigned long long prevIdle = prev.idle + prev.iowait;
    unsigned long long currIdle = curr.idle + curr.iowait;

    unsigned long long prevNonIdle = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;
    unsigned long long currNonIdle = curr.user + curr.nice + curr.system + curr.irq + curr.softirq + curr.steal;

    unsigned long long prevTotal = prevIdle + prevNonIdle;
    unsigned long long currTotal = currIdle + currNonIdle;

    unsigned long long totald = currTotal - prevTotal;
    unsigned long long idled = currIdle - prevIdle;

    return (totald - idled) * 100.0 / totald;
}

int main() {
    CpuTimes prevTimes = getCpuTimes();
    double totalUsage = 0.0;
    int samples = 0;

    for (int i = 0; i < 10000; ++i) { // sample 10 times
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        CpuTimes currTimes = getCpuTimes();
        double usage = calculateCpuUsage(prevTimes, currTimes);
        std::cout << "CPU Usage: " << usage << "%" << std::endl;
        totalUsage += usage;
        prevTimes = currTimes;
        ++samples;
    }

    std::cout << "Average CPU Usage: " << (totalUsage / samples) << "%" << std::endl;
    return 0;
}
