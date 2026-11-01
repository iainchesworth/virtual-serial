//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_MAIN_CPP
#define BOOST_VIRTUAL_SERIAL_TEST_MAIN_CPP

#include "detail/test_framework.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace {

void print_usage(const char* program)
{
    std::printf(
        "usage: %s [--list] [--filter=SUBSTRING] [--help]\n", program);
}

} // namespace

int main(int argc, char** argv)
{
    bool list_only = false;
    std::string filter;

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--list")
            list_only = true;
        else if (argument == "--help" || argument == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (argument.compare(0, 9, "--filter=") == 0)
            filter = argument.substr(9);
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", argument.c_str());
            print_usage(argv[0]);
            return 2;
        }
    }

    std::vector<vs_test::test_case> selected;
    for (const vs_test::test_case& test : vs_test::registry())
    {
        if (!filter.empty()
            && std::string(test.name).find(filter) == std::string::npos)
            continue;
        selected.push_back(test);
    }

    if (list_only)
    {
        for (const vs_test::test_case& test : selected)
            std::printf("%s\n", test.name);
        return 0;
    }

    const std::chrono::steady_clock::time_point started =
        std::chrono::steady_clock::now();
    int failed_cases = 0;
    int passed_cases = 0;

    for (const vs_test::test_case& test : selected)
    {
        const int failures_before = vs_test::failure_count();
        std::printf("[ RUN      ] %s\n", test.name);
        std::fflush(stdout);

        try
        {
            test.function();
        }
        catch (const vs_test::require_failure&)
        {
            // Already reported.
        }
        catch (const std::exception& exception)
        {
            vs_test::report_failure(
                __FILE__, __LINE__,
                std::string("unhandled exception: ") + exception.what());
        }
        catch (...)
        {
            vs_test::report_failure(
                __FILE__, __LINE__, "unhandled unknown exception");
        }

        if (vs_test::failure_count() == failures_before)
        {
            ++passed_cases;
            std::printf("[       OK ] %s\n", test.name);
        }
        else
        {
            ++failed_cases;
            std::printf("[  FAILED  ] %s\n", test.name);
        }
        std::fflush(stdout);
    }

    const std::chrono::milliseconds elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

    std::printf(
        "\n%d test(s) run: %d passed, %d failed (%lld ms)\n",
        passed_cases + failed_cases,
        passed_cases,
        failed_cases,
        static_cast<long long>(elapsed.count()));

    return failed_cases == 0 ? 0 : 1;
}

#endif // BOOST_VIRTUAL_SERIAL_TEST_MAIN_CPP
