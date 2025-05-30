# Manapi Http Server/Client

## Introduction
HTTP server written on C++ which support HTTP/1.1, HTTP/2 and HTTP/3 (over QUIC)

> [!CAUTION]
> This project in development!!!

## About
This HTTP server should simplify development of `web applications`, `API-interfaces` and other things.

So many important utils will be supported out of the box, for example, `JSON`, `MySQL-client`, `PostgreSQL-client`, `JSON-masks`, `Big Int`, `modules`, `plugins`.

## Installation
For compile this project, you need to install below projects:
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
wrk http://127.0.0.1:8889 -d 10 -t 4 -c 200
Running 10s test @ http://127.0.0.1:8889
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     0.90ms  739.74us  17.73ms   79.30%
    Req/Sec    58.46k     4.39k   78.92k    76.25%
  2329117 requests in 10.01s, 306.53MB read
Requests/sec: 232592.51
Transfer/sec:     30.61MB
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

No support (will be soon)

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
    
    auto ctx = manapi::async::context::create(4);
    manapi::async::cthread::current(ctx);
    
    ctx->eventloop()->setup_handle_interrupt();
    
    std::atomic<int> cnt = 0;
    manapi::async::context::run(ctx, 4, [&cnt] (std::function<void> bind) -> void {
        manapi::net::http::server router; 
        manapi::ext::pq::connection db;

        router.GET ("/", [&cnt] (manapi::net::http::req &req, manapi::net::http::resp &resp) mutable -> manapi::future<> {
            co_return resp.text(std::format("Hello World! Count: {}", cnt.fetch_add(1)));
        });
    
        router.GET("/+error", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            resp.replacers({
                {"status_code", std::to_string(resp.status_code())},
                {"status_message", std::string{resp.status_message()}}
            });
    
            co_return resp.file ("../examples/error.html");
        });
    
        router.POST("/+error", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            co_return resp.json({{"error", resp.status_code()},
                    {"msg", std::string{resp.status_message()}}});
        });
    
        router.GET("/cat", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            auto fetch = co_await manapi::net::fetch2::fetch ("https://dragonball-api.com/api/planets/7", {
                {"verify_peer", false},
                {"alpn", true},
                {"method", "GET"}
            });
    
            if (!fetch.ok()) {
                co_return resp.json ({{"error", true}, {"message", "fetch failed"}});
            }
    
            auto data = co_await fetch.json();
    
            co_return resp.text(std::move(data["description"].as_string()));
        });
    
        router.GET("/proxy", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            co_return resp.proxy("http://127.0.0.1:8889/video");
        });
    
        router.GET("/video", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            resp.partial_enabled(true);
            resp.compress_enabled(false);
            co_return resp.file("video.mp4");
        });
    
        router.GET("/stop", [ctx](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await ctx->stop();
            co_return resp.text("stopped");
        });
    
        router.GET("/timeout", [](manapi::net::http::req &req, manapi::net::http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await manapi::async::delay{10000};
            co_return resp.text("10sec");
        });
        
        router.GET("/pq/[id]", [db](manapi::net::http::request& req, manapi::net::http::response& resp) mutable -> manapi::future<> {
            try {
                auto res1 = co_await db.exec("INSERT INTO for_test (id, str_col) VALUES ($2, $1);","no way", std::stoll(req.param("id")));
            }
            catch (...) {
                /* already exists or maybe not... */
            }
            auto res = co_await db.exec("SELECT * FROM for_test;");

            std::string content = "b";
            for (const auto &row: res) {
                content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
            }

            co_return resp.text(std::move(content));
        });
    
        manapi::async::run([router, db] () -> manapi::future<> {
            co_await db.connect("127.0.0.1", "7879", "development", "password", "db");
            co_await router.config_object({
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
            });
            
            co_await router.start();
        });
        
        bind();
    });
  

    return 0;
}
```

## TODO

- [ ] Async
  - [x] Mutex, Conditional Variable, Future  
  - [x] Default Async Context
  - [ ] ⭐ Some improvements
- [ ] Debugging
  - [x] Error Codes
  - [ ] Stack Error
  - [x] Async I/O Debug
- [ ] Configuration
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
  - [ ] OpenSSL
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
- Tested on Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland
