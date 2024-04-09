#include <hdr/hdr_histogram.h>
#include <string>
#include <sstream>
#include <vector>
#include <regex>
class Histogram {
public:
    explicit Histogram(int64_t lowest, int64_t highest, int sigfigs, double scale = 1);

    explicit Histogram(int64_t lowest, int64_t highest, double scale = 1);

    ~Histogram();

    void reset();

    void print(FILE *stream, int32_t ticks);

    void print_csv(FILE *stream, int32_t ticks);

    std::string get_pretty_print(int32_t ticks);

    void record(int64_t value, int64_t count = 0);

    void record_atomic(int64_t value, int64_t count = 0);

private:
    hdr_histogram *latency_hist;
    double scale_value{ 1 };
};
