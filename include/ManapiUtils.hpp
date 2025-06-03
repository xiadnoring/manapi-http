#pragma once

#define NOMINMAX

#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <queue>
#include <list>
#include <forward_list>
#include <typeinfo>
#include "ManapiInt.hpp"
#include "ManapiParams.hpp"

//#include "./extensions/jemallocator.hpp"

#define REQ(_x) manapi::net::http::request &_x
#define RESP(_x) manapi::net::http::response &_x

#define HANDLER(_req, _resp) (REQ(_req), RESP(_resp))

#ifdef _WIN32
#   define MANAPIHTTP_NONUNIX true
#else
#   define MANAPIHTTP_NONUNIX false
#endif

namespace manapi::sockets {
    enum ip_version {
        IP_VERSION_4 = 4,
        IP_VERSION_6 = 6
    };
}

namespace manapi::memory {
    template<typename T>
    constexpr T *alloc (std::size_t size) {
        auto p = static_cast<T *> (::malloc(size));
        if (!p) { throw std::bad_alloc(); }
        return p;
    }

    inline void free (void *p) {
        ::free(p);
    }

    template<typename T>
    constexpr T *realloc (T *n, std::size_t size) {
        auto p = static_cast<T *> (::realloc(n, size));
        if (!p) { throw std::bad_alloc(); }
        return p;
    }
}


// namespace manapi::stl {
//     /*
//      * Useful allocator alias
//      */
//     template<typename T>
//     using basic_allocator = ::jemallocator::jemallocator<T, jemallocator::jepolicy::empty::policy>;
//
//     template<typename T>
//     using default_allocator = basic_allocator<T>;
//
//     /*
//      * Useful container aliases
//      */
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using forward_list = std::forward_list<T, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using deque = std::deque<T, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using list = std::list<T, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using vector = std::vector<T, Alloc>;
//
//     template<typename Key, typename T, class Alloc = default_allocator<std::pair<const Key, T>>>
//     using map = std::map<Key, T, std::less<Key>, Alloc>;
//
//     template<typename Key, typename T, class Alloc = default_allocator<std::pair<const Key, T>>>
//     using multimap = std::multimap<Key, T, std::less<Key>, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using multiset = std::multiset<T, std::less<T>, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using set = std::set<T, std::less<T>, Alloc>;
//
//     template<typename Key, typename T, class Alloc = default_allocator<std::pair<const Key, T>>>
//     using unordered_map = std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>, Alloc>;
//
//     template<typename Key, typename T, class Alloc = default_allocator<std::pair<const Key, T>>>
//     using unordered_multimap = std::unordered_multimap<Key, T, std::hash<Key>, std::equal_to<Key>, Alloc>;
//
//     template<typename Key, class Alloc = default_allocator<Key>>
//     using unordered_multiset = std::unordered_multiset<Key, std::hash<Key>, std::equal_to<Key>, Alloc>;
//
//     template<typename Key, class Alloc = default_allocator<Key>>
//     using unordered_set = std::unordered_set<Key, std::hash<Key>, std::equal_to<Key>, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using stack = std::stack<T, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using queue = std::queue<T, Alloc>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using priority_queue = std::priority_queue<T, std::vector<T, Alloc>, std::less<typename std::vector<T, Alloc>::value_type>>;
//
//     template<typename T, class Alloc = default_allocator<T>>
//     using basic_string = std::basic_string<T, std::char_traits<T>, Alloc>;
//
//     using string = basic_string<char>;
//     using wstring = basic_string<wchar_t>;
//     using u16string = basic_string<char16_t>;
//     using u32string = basic_string<char32_t>;
// }