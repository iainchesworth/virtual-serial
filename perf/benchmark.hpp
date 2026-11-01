//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// A dependency-free benchmark harness: results are emitted as JSON so CI can
// publish them as an artifact, render a Markdown table and compare them with
// the floors in perf/floors.json.
//

#ifndef BOOST_VIRTUAL_SERIAL_PERF_BENCHMARK_HPP
#define BOOST_VIRTUAL_SERIAL_PERF_BENCHMARK_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace perf {

using clock_type = std::chrono::steady_clock;

inline double seconds_since(clock_type::time_point start)
{
    return std::chrono::duration<double>(clock_type::now() - start).count();
}

inline double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    double value = values[middle];
    if (values.size() % 2 == 0)
    {
        const double lower =
            *std::max_element(values.begin(), values.begin() + middle);
        value = (value + lower) / 2.0;
    }
    return value;
}

inline double percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(position);
    const std::size_t upper = std::min(lower + 1, values.size() - 1);
    const double weight = position - static_cast<double>(lower);
    return values[lower] * (1.0 - weight) + values[upper] * weight;
}

struct environment
{
    std::string os;
    std::string compiler;
    std::string cxx_standard;
    std::string boost_version;
    std::string build_type;
};

struct result
{
    std::string name;
    std::string unit;   // "MB/s", "ops/s" or "us"
    std::string better; // "higher" or "lower"
    double value = 0.0;
    std::size_t iterations = 0;
    std::vector<double> samples;
    std::string note;
};

inline std::string json_escape(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char character : text)
    {
        switch (character)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += character;
        }
    }
    return out;
}

inline std::string timestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm parts;
#if defined(_WIN32)
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}

class suite
{
public:
    explicit suite(bool quick)
        : quick_(quick)
    {
    }

    bool quick() const
    {
        return quick_;
    }

    void add(result value)
    {
        results_.push_back(std::move(value));
    }

    const std::vector<result>& results() const
    {
        return results_;
    }

    void print_table(std::ostream& out) const
    {
        out << std::left << std::setw(34) << "benchmark" << std::right
            << std::setw(14) << "value" << "  " << "unit" << '\n';
        out << std::string(62, '-') << '\n';
        for (const result& entry : results_)
        {
            out << std::left << std::setw(34) << entry.name << std::right
                << std::setw(14) << std::setprecision(6) << entry.value << "  "
                << entry.unit;
            if (!entry.note.empty())
                out << "  (" << entry.note << ")";
            out << '\n';
        }
    }

    void write_json(const environment& env, const std::string& path) const
    {
        std::ofstream file(path);
        file << "{\n";
        file << "  \"schema\": 1,\n";
        file << "  \"generated_at\": \"" << timestamp() << "\",\n";
        file << "  \"environment\": {\n";
        file << "    \"os\": \"" << json_escape(env.os) << "\",\n";
        file << "    \"compiler\": \"" << json_escape(env.compiler) << "\",\n";
        file << "    \"cxx_standard\": \"" << json_escape(env.cxx_standard)
             << "\",\n";
        file << "    \"boost_version\": \"" << json_escape(env.boost_version)
             << "\",\n";
        file << "    \"build_type\": \"" << json_escape(env.build_type) << "\"\n";
        file << "  },\n";
        file << "  \"results\": [\n";
        for (std::size_t index = 0; index < results_.size(); ++index)
        {
            const result& entry = results_[index];
            file << "    {\n";
            file << "      \"name\": \"" << json_escape(entry.name) << "\",\n";
            file << "      \"unit\": \"" << json_escape(entry.unit) << "\",\n";
            file << "      \"better\": \"" << json_escape(entry.better) << "\",\n";
            file << "      \"value\": " << std::setprecision(12) << entry.value
                 << ",\n";
            file << "      \"iterations\": " << entry.iterations << ",\n";
            file << "      \"samples\": [";
            for (std::size_t sample = 0; sample < entry.samples.size(); ++sample)
            {
                if (sample)
                    file << ", ";
                file << std::setprecision(12) << entry.samples[sample];
            }
            file << "],\n";
            file << "      \"note\": \"" << json_escape(entry.note) << "\"\n";
            file << "    }" << (index + 1 == results_.size() ? "" : ",") << '\n';
        }
        file << "  ]\n";
        file << "}\n";
    }

private:
    bool quick_;
    std::vector<result> results_;
};

} // namespace perf

#endif // BOOST_VIRTUAL_SERIAL_PERF_BENCHMARK_HPP
