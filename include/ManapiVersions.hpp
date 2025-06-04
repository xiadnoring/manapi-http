#pragma once

#include <cstdint>
#include <array>
#include <string>
#include "ManapiUtils.hpp"

template<size_t Size>
using static_string = std::array<const char, Size>;

template<size_t ... Indexes>
struct index_sequence {};

template<size_t Size, size_t ... Indexes>
struct make_index_sequence : make_index_sequence<Size - 1, Size - 1, Indexes ...> {};

template<size_t ... Indexes>
struct make_index_sequence<0, Indexes ...> : index_sequence<Indexes ...> {};

template<size_t Size, size_t ... Indexes>
constexpr static_string<sizeof ... (Indexes) + 1> make_static_string(const char (& str)[Size],
    index_sequence<Indexes ...>) {
    return {str[Indexes] ..., '\0'};
}

constexpr static_string<1> make_static_string() {
    return {'\0'};
}

template<size_t Size>
constexpr static_string<Size> make_static_string(const char (& str)[Size]) {
    return make_static_string(str, make_index_sequence<Size - 1>{});
}

template<size_t Size>
std::string to_string(const static_string<Size>& str) {
    return std::string(str.data());
}

template<size_t Size1, size_t Size2>
constexpr int static_string_compare(
    const static_string<Size1>& str1,
    const static_string<Size2>& str2,
    int index = 0) {
    return index >= Size1 && index >= Size2 ? 0 :
        index >= Size1 ? -1 :
            index >= Size2 ? 1 :
                str1[index] > str2[index] ? 1 :
                    str1[index] < str2[index] ? -1 :
                        static_string_compare(str1, str2, index + 1);
}

template<size_t Size1, size_t Size2>

constexpr int version_compare_job_(
    const static_string<Size1>& str1,
    const static_string<Size2>& str2,
    int index, int major1, int major2, int minor1,
    int minor2, int patch1, int patch2, int i1,int i2) {
    return index == std::max(Size1, Size2) ? (major1 == major2 ?
        (
            (minor1 == minor2) ?
            (
                (patch1 == patch2) ? (0) : (patch1 > patch2 ? -1 : 1)
            )
            :
            (minor1 > minor2 ? -1 : 1)
        )
        :
        (major1 > major2 ? -1 : 1))
    :
    /* parse */
    (
        ((
            ((index < Size1) ? (
                str1[index]=='.' ?
                (i1++)
                :
                (
                    (str1[index] >= '0' && str1[index] <= '9') ?
                    (
                        i1 == 0 ? ((major1 *= 10) += (str1[index] - '0'))
                        :
                        (
                            (i1 == 1) ?
                            ((minor1 *= 10) += (str1[index] - '0'))
                            :
                            (
                                (i1 == 2) ?
                                ((patch1 *= 10) += (str1[index] - '0'))
                                :
                                (false)
                            )
                        )
                    )
                    :
                    (i1=3)
                )
            ) : false)
            &
            ((index < Size2) ? (
                str2[index]=='.' ?
                (i2++)
                :
                (
                    (str2[index] >= '0' && str2[index] <= '9') ?
                        (
                        i2 == 0 ? ((major2 *= 10) += (str2[index] - '0'))
                        :
                        (
                            (i2 == 1) ?
                            ((minor2 *= 10) += (str2[index] - '0'))
                            :
                            (
                                (i2 == 2) ?
                                ((patch2 *= 10) += (str2[index] - '0'))
                                :
                                (false)
                            )
                        )
                    )
                    :
                    (i2=3)
                )
            ) : false)
        ) | true) ? version_compare_job_(str1, str2, index+1, major1, major2, minor1,minor2, patch1,patch2,i1,i2) : 0
    );
}

template<size_t Size1, size_t Size2>
constexpr int version_compare_(
    const static_string<Size1>& str1,
    const static_string<Size2>& str2,
    int index = 0, int major1 = 0, int major2 = 0, int minor1=0,
    int minor2=0, int patch1=0, int patch2=0, int i1=0,int i2=0) {
    return version_compare_job_(str1, str2, index, major1, major2, minor1, minor2, patch1, patch2, i1, i2);
}

template<size_t Size1, size_t Size2>
constexpr int version_compare(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare_(make_static_string<Size1>(str1), make_static_string<Size2>(str2));
}

template<size_t Size1, size_t Size2>
constexpr bool version_equal(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare<Size1, Size2>(str1, str2) == 0;
}

template<size_t Size1, size_t Size2>
constexpr bool version_less(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare<Size1, Size2>(str1, str2) == 1;
}

template<size_t Size1, size_t Size2>
constexpr bool version_greater(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare<Size1, Size2>(str1, str2) == -1;
}

template<size_t Size1, size_t Size2>
constexpr bool version_less_or_equal(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare<Size1, Size2>(str1, str2) != -1;
}

template<size_t Size1, size_t Size2>
constexpr bool version_greater_or_equal(const char (& str1)[Size1], const char (& str2)[Size2]) {
    return version_compare<Size1, Size2>(str1, str2) != 1;
}