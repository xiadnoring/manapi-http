# <img src="assets/logo.png" alt="logo" style="width:20px;vertical-align:middle; margin-left:10px;"> <span>Manapi Http</span>


## Introduction
HTTP server written on C++ which supports HTTP/1.1, HTTP/2 and HTTP/3 (over QUIC)

> [!CAUTION]
> This project is in development!!!

## About
![console](/assets/console1.png)

This HTTP server should simplify development of `web applications`, `API-interfaces` and other things.

Many important utils will be supported out of the box, for example, `JSON`, `MySQL-client`, `PostgreSQL-client`, `JSON-masks`, `Big Int`, `modules`, `plugins`.

## Installation

To compile this project, you need to install below projects:
- OpenSSL 3.3.1 or greater \[optional\]
- zlib 1.3.1 or greater \[optional\]
- gmp 6.3.0 or greater \[optional\]
- **libuv 1.49.2 or greater \[required\]**
- curl 8.8.0-1 or greater \[optional\]
- wolfssl 5.5.0 or greater \[optional\]
- quiche 0.22.0 or greater \[optional\]
- tquic 1.5.0 or greater \[optional\]

### Benchmark

Using 16 threads on my laptop (Intel i5-12500H) wrk (HTTP/1.1) gave me the following results

```bash
wrk http://0.0.0.0:8889/main -d 10 -t 4 -c 200
Running 10s test @ http://127.0.0.1:8889/main
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   282.72us  531.03us  19.43ms   95.73%
    Req/Sec   203.80k    20.56k  286.61k    76.00%
  8106400 requests in 10.02s, 1.28GB read
Requests/sec: 809023.73
Transfer/sec:    131.16MB
```

### For Arch Linux
```bash
pacman -Sy libuv
```

```bash
# optinal
pacman -Sy gmp openssl zlib curl
```

or 

```bash
paru -Sy libuv
```

```bash
paru -Sy gmp openssl zlib curl
```

### For Windows

MSVC C++ (Tested Executable Only)

### For MacOs

No support (will be soon)

## Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug/Release ...
```

### Build as Executable
```bash
cmake ... -DMANAPIHTTP_BUILD_TYPE=exe
```

### Build as Library
```bash
cmake ... -DMANAPIHTTP_BUILD_TYPE=lib
```

### Build with Conan
```bash
cmake ... -DMANAPIHTTP_BUILD_METHOD=conan
```

## Example

```c++
int main () {
    manapi::async::context::threadpoolfs(2);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    auto ctx = manapi::async::context::create(4).unwrap();
    ctx->eventloop()->setup_handle_interrupt();

    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();
    
    std::atomic<int> cnt = 0;
    
    manapi::async::context::run(ctx, 4, [&cnt, router_ctx] (std::function<void()> bind) -> void {
        using http = manapi::net::http::server;
        auto router = manapi::net::http::server::create(router_ctx).unwrap();
        auto db = manapi::ext::pq::connection::create().unwrap();

        router.GET ("/", [&cnt] (http::req &req, manapi::net::http::response *resp) mutable -> void {
            resp->text(std::format("Hello World! Count: {}", cnt.fetch_add(1))).unwrap();
            resp->finish();
        }).unwrap();

        router.GET("/+error", [](http::req &req, http::resp &resp) -> manapi::future<> {
            resp.replacers({
                {"status_code", std::to_string(resp.status_code())},
                {"status_message", std::string{resp.status_message()}}
            }).unwrap();

            co_return resp.file ("../examples/error.html").unwrap();
        }).unwrap();

        router.POST("/+error", [](http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.json({{"error", resp.status_code()},
                    {"msg", std::string{resp.status_message()}}}).unwrap();
        }).unwrap();

        router.GET("/cat", [](http::req &req, http::resp &resp) -> manapi::future<> {
            auto fetch = (co_await manapi::net::fetch2::fetch ("https://dragonball-api.com/api/planets/7", {
                {"verify_peer", false},
                {"alpn", true},
                {"method", "GET"}
            })).unwrap();

            if (!fetch.ok()) {
                co_return resp.json ({{"error", true}, {"message", "fetch failed"}}).unwrap();
            }

            auto data = (co_await fetch.json()).unwrap();

            co_return resp.text(std::move(data["description"].as_string())).unwrap();
        }).unwrap();

        router.GET("/proxy", [](http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.proxy("http://127.0.0.1:8889/video").unwrap();
        }).unwrap();

        router.GET("/video", [](http::req &req, http::resp &resp) -> manapi::future<> {
            resp.partial_enabled(true);
            resp.compress_enabled(false);
            co_return resp.file("video.mp4").unwrap();
        }).unwrap();

        router.GET("/stop", [](http::req &req, http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await manapi::async::current()->stop();
            co_return resp.text("stopped").unwrap();
        }).unwrap();

        router.GET("/timeout", [](http::req &req, http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await manapi::async::delay{10000};
            co_return resp.text("10sec").unwrap();
        }).unwrap();

        router.GET("/pq/[id]", [db](manapi::net::http::request& req, manapi::net::http::response& resp) mutable -> manapi::future<> {
            auto msg = req.param("id").unwrap();
            char *end;
            auto res1 = co_await db.exec("INSERT INTO for_test (id, str_col) VALUES ($2, $1);","no way", std::strtoll(msg.data(), &end, 10));
            if (!res1) {
                if (res1.sqlcode() != manapi::ext::pq::SQL_STATE_UNIQUE_VIOLATION)
                    res1.err().log();
            }

            auto res = co_await db.exec("SELECT * FROM for_test;");
            if (res) {
                std::string content = "b";
                for (const auto &row: res.unwrap()) {
                    content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
                }

                co_return resp.text(std::move(content)).unwrap();
            }

            co_return resp.text(std::string{res.is_sqlerr() ? res.sqlmsg() : res.message()}).unwrap();
        }).unwrap();

        manapi::async::run([router, db] () mutable -> manapi::future<> {
            (co_await db.connect("127.0.0.1", "7879", "development", "password", "db")).unwrap();
            (co_await router.config_object({
                {"pools", manapi::json::array({
                    {
                        {"address", "127.0.0.1"},
                        {"http_versions", manapi::json::array({"2", "1.1"})},
                        {"transport", "tls"},
                        {"partial_data_min_size", 0},
                        {"tls_version", "1.3"},
                        {"implementation", "openssl"},
                        {"port", "8888"},
                        {"ssl", {
                            {"cert", "../examples/self-signed-ssl/cert.crt"},
                            {"key", "../examples/self-signed-ssl/cert.key"},
                            {"enabled", true}
                        }},
                        {"tcp_no_delay", true}
                    }
                })},
                {"save_config", false}
            })).unwrap();

            (co_await router.start()).unwrap();
        });

        bind();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}
```

## Params

| Option name                         | Description              | Values                  |
|:------------------------------------|:-------------------------|:------------------------|
| MANAPIHTTP_GMP_DEPENDENCY           | Bigint Support           | **ON**/OFF              |
| MANAPIHTTP_CURL_DEPENDENCY          | Fetch Support            | **ON**/OFF              |
| MANAPIHTTP_ZLIB_DEPENDENCY          | gzip/deflate Support     | **ON**/OFF              |
| MANAPIHTTP_QUICHE_DEPENDENCY        | HTTP3/QUIC by Cloudflare | **ON**/OFF              | 
| MANAPIHTTP_OPENSSL_DEPENDENCY       | TLS/QUIC by OpenSSL      | **ON**/OFF              |
| MANAPIHTTP_WOLFSSL_DEPENDENCY       | TLS by WolfSSL           | ON/**OFF**              |
| MANAPIHTTP_GRPC_DEPENDENCY          | gRPC Support             | **ON**/OFF              |
| MANAPIHTTP_BROTLI_DEPENDENCY        | brotli Support           | **ON**/OFF              |
| MANAPIHTTP_ZSTD_DEPENDENCY          | zstd Support             | **ON**/OFF              |
| MANAPIHTTP_WOLFSSL_WITH_ALPN        | WolfSSL with ALPN        | **ON**/OFF              |
| MANAPIHTTP_NGHTTP2_DEPENDENCY       | HTTP2 by nghttp2         | **ON**/OFF              |
| MANAPIHTTP_NGHTTP3_DEPENDENCY       | HTTP3 by nghttp3         | **ON**/OFF              |
| MANAPIHTTP_CPPTRACE_DEPENDENCY      | CppTrace                 | ON/**OFF**              |
| MANAPIHTTP_JSON_DEBUG               | JSON debug symbols       | **ON**/OFF              |
| MANAPIHTTP_STD_BACKTRACE_DEPENDENCY | STD C++23 stacktrace     | **ON**/OFF              |
| BUILD_SHARED_LIBS                   | Build as a shared lib    | ON/**OFF**              |
| MANAPIHTTP_BUILD_METHOD             | Build Method             | conan/**default**/fetch | 
| MANAPIHTTP_DEPENDENCY_FETCH_SHARED  | Bsuild shared depends    | ON/**OFF**              |
| MANAPIHTTP_OPENSSL_SYSTEM_DISABLE   | Disable default OpenSSL  | ON/**OFF**              |
| MANAPIHTTP_CURL_SYSTEM_DISABLE      | Disable default cURL     | ON/**OFF**              |
| MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT  | Depends Output Path      | \<PATH\>/**OFF**        |
| MANAPIHTTP_BUILD_TYPE               | Binary or Library        | exe/**lib**/test        |
| MANAPIHTTP_INSTALL_ARCH             | '/x86_64-linux-gnu'      | '/\<PATH\>'/**OFF**      |


## TODO

- [x] Async
  - [x] Mutex, Conditional Variable, Future  
  - [x] Default Async Context
  - [x] ⭐ Some improvements
- [x] Debugging
  - [x] Error Codes
  - [x] Stack Error
  - [x] Async I/O Debug
- [x] Configuration
  - [x] limit-rate (TCP: HTTP/1.1, HTTP/2)
  - [x] limit-rate (UDP: HTTP/3)
  - [x] minimum speed requirements
- [ ] HTTP
  - [x] Default HTTP/1.1 realization
  - [x] Default HTTP/2 realization
  - [ ] Default HTTP/3 realization
  - [x] Support HTTP/1.1
  - [x] Support HTTP/2
  - [x] Support HTTP/3
- [ ] HTTP Features
  - [x] Chunked Transmission (HTTP/1.1 - 3)
  - [x] Ranges 
  - [x] FormData
  - [x] JSON (chunked transmission)
  - [ ] Multi-Ranges
- [ ] TLS
  - [ ] Default realization
  - [x] OpenSSL
  - [x] WolfSSL
  - [ ] BoringSSL
- [ ] QUIC
  - [ ] Default realization 
  - [x] quiche
  - [ ] tquic
  - [x] OpenSSL
  - [ ] WolfSSL
  - [ ] BoringSSL
- [x] Fetch
  - [x] Async CURL support
  - [x] Async/sync read callbacks
  - [x] Async/sync write callbacks
  - [x] Chunked Transmission (HTTP/1.1 - 3)
- [ ] Other Protocols
  - [ ] WebSockets
- [ ] Cross-Platform Build
  - [x] Linux
  - [ ] MacOs
  - [ ] Windows

## Tested
- Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland Debug/Release
- Windows 11 Pro 22h2 x86_64 MSVC Debug (exe)

## Made in

[![ГБОУ РИЛИ](assets/rili.png)](https://rilirb.ru)

[![Башкортостан](assets/rb2.png)](https://ru.wikipedia.org/wiki/%D0%91%D0%B0%D1%88%D0%BA%D0%BE%D1%80%D1%82%D0%BE%D1%81%D1%82%D0%B0%D0%BD)