#include <iostream>
#include <memory>

#include "ManapiInitTools.hpp"
#include "ManapiEventLoop.hpp"
#include "ManapiDebug.hpp"
#include "ManapiProcess.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY
#   include "ManapiGrpc.hpp"
#endif

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#   include <openssl/ssl.h>
#   include <openssl/rand.h>
#   include <openssl/err.h>
#   include <openssl/bio.h>
#   include <openssl/engine.h>
#endif

#if MANAPIHTTP_CURL_DEPENDENCY
#   include <curl/curl.h>
#endif

// static int cnt = 0;
//
// static std::map<std::pair<char*, int>, std::pair<const char*, int>> allocated;

void *crypto_malloc (size_t num, const char *file, int line) {
    //cnt++;
    auto const ctx = manapi::async::current().get();
    if (ctx) {
        auto res = manapi::async::current()->memory_fabric().alloc(num);
        //allocated.insert({{(char*)(res) - 1, (int)(*((char*)res-1))}, {file, line}});
        return res;
    }
    auto p = static_cast<char*>(::malloc(num + 1));
    *p = '\0';
    return (p+1);
}

void *crypto_realloc (void *addr, size_t num, const char *file, int line) {
    auto const ctx = manapi::async::current().get();
    if (ctx) {
        //if (addr)
            //allocated.erase({(char*)(addr) - 1, (int)(*((char*)addr-1))});
        //auto res = manapi::async::current()->memory_fabric().realloc(addr, num);

        //allocated.insert({{(char*)(res) - 1, (int)(*((char*)res-1))}, {file, line}});
        //return res;
        return manapi::async::current()->memory_fabric().realloc(addr, num);
    }

    if (addr) {
        if (*(static_cast<char*>(addr)-1))
            return nullptr;

        addr = (static_cast<char*>(addr) - 1);
    }
    auto p = static_cast<char*>(::realloc(addr, num + 1));
    *p = '\0';
    return (p+1);
}

void crypto_free (void *addr, const char *file, int line) {
    if (addr) {
    //cnt--;
        //allocated.erase({(char*)(addr) - 1, (int)(*((char*)addr-1))});
        auto const ctx = manapi::async::current().get();
        if (ctx)
            return manapi::async::current()->memory_fabric().free(addr);
        if (!*(static_cast<char*>(addr)-1))
            ::free((static_cast<char*>(addr)-1));
    }
}

void manapi::init_tools::ssl_library_init() {
    int rhs;

    auto res = process::get_env("MANAPIHTTP_CRYPTO_MALLOC");
    bool const crypto_custom_malloc = res.ok() ? std::stoi(res.unwrap()) : false;

#if MANAPIHTTP_OPENSSL_DEPENDENCY
        if (crypto_custom_malloc) {
            if ((rhs = CRYPTO_set_mem_functions(crypto_malloc, crypto_realloc, crypto_free)) != 1)
                manapi_log_error("openssl: CRYPTO_set_mem_functions() returned %d", rhs);
        }
        OpenSSL_add_ssl_algorithms();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
        RAND_poll();
#endif
}
void manapi::init_tools::ev_library_init() {
    // ev::set_allocator([] (void *ptr, long size) noexcept
    //     -> void * {
    //     if (ptr) {
    //         return ::realloc(ptr, size);
    //     }
    //
    //     return ::malloc(size);
    // });
    //allocated.clear();
}

void manapi::init_tools::curl_library_init() {
#if MANAPIHTTP_CURL_DEPENDENCY
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

void manapi::init_tools::log_trace_init(manapi::debug::trace_level lvl) {
    debug::log_trace_enabled = lvl;
}

void manapi::clear_tools::grpc_clear() MANAPIHTTP_NOEXCEPT {
#if MANAPIHTTP_GRPC_DEPENDENCY
    net::wgrpc::server_ctx::clean();
#endif
}

void manapi::clear_tools::ssl_library_thread_clear() MANAPIHTTP_NOEXCEPT {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    OPENSSL_thread_stop();
#endif
}

void manapi::clear_tools::ssl_library_clear() MANAPIHTTP_NOEXCEPT {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    OPENSSL_thread_stop();
    OPENSSL_cleanup();
    ENGINE_cleanup();
    CONF_modules_unload(1);
    ERR_free_strings();
    EVP_cleanup();
    sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
    //SSL_COMP_free_compression_methods();
    CRYPTO_cleanup_all_ex_data();
#endif
}

void manapi::clear_tools::ev_library_clear() MANAPIHTTP_NOEXCEPT {

}

void manapi::clear_tools::curl_library_clear() MANAPIHTTP_NOEXCEPT {
#if MANAPIHTTP_CURL_DEPENDENCY
    curl_global_cleanup();
#endif
}
