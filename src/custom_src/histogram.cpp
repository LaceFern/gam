#include "histogram.h"

Histogram::Histogram(int64_t lowest, int64_t highest, int sigfigs, double scale) {
    hdr_init(lowest, highest, sigfigs, &latency_hist);
    scale_value = scale;
}

Histogram::Histogram(int64_t lowest, int64_t highest, double scale) {
    hdr_init(lowest, highest, 3, &latency_hist);
    scale_value = scale;
}


Histogram::~Histogram() {
    hdr_close(latency_hist);
}

void Histogram::reset() {
    hdr_reset(latency_hist);
}


void Histogram::print(FILE *stream, int32_t ticks) {
    hdr_percentiles_print(latency_hist, stream, ticks, scale_value, CLASSIC);
}

void Histogram::print_csv(FILE *stream, int32_t ticks) {
    hdr_percentiles_print(latency_hist, stream, ticks, scale_value, CSV);
}

std::string Histogram::get_pretty_print(int32_t ticks) {
    double result_99 = -1;
    double result_avg = -1;
    double result_count = -1;

    char *tmp_buf = nullptr;
    size_t tmp_size = 0;

    FILE *fp = open_memstream(&tmp_buf, &tmp_size);
    hdr_percentiles_print(latency_hist, fp, ticks, scale_value, CLASSIC);
    fclose(fp);

    std::istringstream result_ss(tmp_buf);
    std::string line;

    std::getline(result_ss, line); // Skip first line
    std::getline(result_ss, line); // Skip second line
    while (std::getline(result_ss, line)) {
        if (line[0] != '#') {
            std::istringstream iss(line);
            std::string word;
            std::vector<std::string> wordlist;

            while (iss >> word) {
                wordlist.push_back(word);
            }

            double value = std::stod(wordlist[0]);
            double percentile = std::stod(wordlist[1]);

            if (percentile >= 0.99 && result_99 == -1) {
                result_99 = value;
            }
        } else {
            std::smatch match;
            if (std::regex_search(line, match, std::regex(R"(\bMean\s*=\s*([-+]?\d*\.\d+|\d+))"))) {
                result_avg = std::stod(match[1]);
            }
            if (std::regex_search(line, match, std::regex(R"(\bTotal count\s*=\s*([-+]?\d*\.\d+|\d+))"))) {
                result_count = std::stod(match[1]);
            }
        }
    }

    free(tmp_buf);
    return std::to_string(result_count) + "\t" + std::to_string(result_avg) + "\t" + std::to_string(result_99) + "\n";
}

void Histogram::record(int64_t value, int64_t count) {
    if (count == 0) {
        count = 1;
    }
    value = value * scale_value;
    hdr_record_values(latency_hist, value, count);
}

void Histogram::record_atomic(int64_t value, int64_t count) {
    if (count == 0) {
        count = 1;
    }
    value = value * scale_value;
    hdr_record_values_atomic(latency_hist, value, count);
}