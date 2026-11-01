//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_DETAIL_TEST_FRAMEWORK_HPP
#define BOOST_VIRTUAL_SERIAL_TEST_DETAIL_TEST_FRAMEWORK_HPP

// A very small, dependency-free test framework.
//
// The library is header-only and its tests must build with nothing but a
// C++14 compiler and the Boost headers, so a self-contained framework is
// preferable to pulling in a compiled test library.  It provides the four
// things the suite actually needs: registration, assertions that keep
// going, assertions that abort, and exception checks.

#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace vs_test {

struct test_case
{
    const char* name;
    void (*function)();
};

inline std::vector<test_case>& registry()
{
    static std::vector<test_case> cases;
    return cases;
}

struct registrar
{
    registrar(const char* name, void (*function)())
    {
        registry().push_back(test_case{name, function});
    }
};

inline int& failure_count()
{
    static int count = 0;
    return count;
}

inline void report_failure(
    const char* file, int line, const std::string& message)
{
    ++failure_count();
    std::fprintf(stderr, "    %s:%d: %s\n", file, line, message.c_str());
}

/// Thrown by VS_REQUIRE to abandon the current test case.
class require_failure : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "VS_REQUIRE failed";
    }
};

template <class T, class = void>
struct is_streamable : std::false_type
{
};

template <class T>
struct is_streamable<
    T,
    decltype(void(std::declval<std::ostream&>() << std::declval<const T&>()))>
    : std::true_type
{
};

template <class T>
inline typename std::enable_if<is_streamable<T>::value, std::string>::type
to_text(const T& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

template <class T>
inline typename std::enable_if<!is_streamable<T>::value, std::string>::type
to_text(const T&)
{
    return "<non-printable>";
}

template <class L, class R>
inline void check_equal(
    const L& lhs,
    const R& rhs,
    const char* file,
    int line,
    const char* text,
    bool expect_equal)
{
    if ((lhs == rhs) == expect_equal)
        return;
    report_failure(
        file,
        line,
        std::string(expect_equal ? "CHECK_EQ(" : "CHECK_NE(") + text
            + ") failed: " + to_text(lhs) + (expect_equal ? " != " : " == ")
            + to_text(rhs));
}

template <class L, class R>
inline void check_less(
    const L& lhs,
    const R& rhs,
    const char* file,
    int line,
    const char* text)
{
    if (lhs < rhs)
        return;
    report_failure(
        file,
        line,
        std::string("CHECK_LT(") + text + ") failed: " + to_text(lhs)
            + " >= " + to_text(rhs));
}

} // namespace vs_test

#define VS_TEST_CASE(name)                                              \
    static void name();                                                 \
    static ::vs_test::registrar name##_registrar(#name, &name);         \
    static void name()

#define VS_CHECK(expression)                                            \
    do                                                                  \
    {                                                                   \
        if (!(expression))                                              \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__, "CHECK(" #expression ")");          \
    } while (false)

#define VS_REQUIRE(expression)                                          \
    do                                                                  \
    {                                                                   \
        if (!(expression))                                              \
        {                                                               \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__, "REQUIRE(" #expression ")");        \
            throw ::vs_test::require_failure();                         \
        }                                                               \
    } while (false)

#define VS_CHECK_EQ(lhs, rhs)                                           \
    ::vs_test::check_equal(                                             \
        (lhs), (rhs), __FILE__, __LINE__, #lhs ", " #rhs, true)

#define VS_CHECK_NE(lhs, rhs)                                           \
    ::vs_test::check_equal(                                             \
        (lhs), (rhs), __FILE__, __LINE__, #lhs ", " #rhs, false)

#define VS_CHECK_LT(lhs, rhs)                                           \
    ::vs_test::check_less((lhs), (rhs), __FILE__, __LINE__, #lhs ", " #rhs)

#define VS_CHECK_NO_THROW(expression)                                   \
    do                                                                  \
    {                                                                   \
        try                                                             \
        {                                                               \
            expression;                                                 \
        }                                                               \
        catch (const std::exception& exception)                         \
        {                                                               \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__,                                     \
                std::string("NO_THROW(" #expression ") threw: ")        \
                    + exception.what());                                \
        }                                                               \
        catch (...)                                                     \
        {                                                               \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__,                                     \
                "NO_THROW(" #expression ") threw an unknown exception");\
        }                                                               \
    } while (false)

#define VS_CHECK_THROW(expression, exception_type)                      \
    do                                                                  \
    {                                                                   \
        bool caught_expected = false;                                   \
        try                                                             \
        {                                                               \
            expression;                                                 \
        }                                                               \
        catch (const exception_type&)                                   \
        {                                                               \
            caught_expected = true;                                     \
        }                                                               \
        catch (...)                                                     \
        {                                                               \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__,                                     \
                "THROW(" #expression ") threw the wrong type");         \
            caught_expected = true;                                     \
        }                                                               \
        if (!caught_expected)                                           \
            ::vs_test::report_failure(                                  \
                __FILE__, __LINE__,                                     \
                "THROW(" #expression ") did not throw");                \
    } while (false)

#define VS_FAIL(message)                                                \
    ::vs_test::report_failure(__FILE__, __LINE__, (message))

#endif // BOOST_VIRTUAL_SERIAL_TEST_DETAIL_TEST_FRAMEWORK_HPP
