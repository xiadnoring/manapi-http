# Manapi HTTP Server/Client

## Introduction
HTTP server written on C++ which support HTTP/1.1, HTTP/2 and HTTP/3 (over QUIC)

> [!CAUTION]
> This project in development!!!

## About
This HTTP server need to make easy develop `web applications`, `API-interfaces` and other things.

So many important utils will be supported out of the box, for example, `JSON`, `MySQL-client`, `PostgreSQL-client`, `JSON-masks`, `Big Int`, `modules`, `plugins`.

## Installation
For compile this project, you need to install below projects:
- OpenSSL 3.3.1 or greater \[optional\]
- zlib 1.3.1 or greater \[optional\]
- gmp 6.3.0 or greater \[optional\]
- **libev 4.33-3 or greater \[required\]**
- curl 8.8.0-1 or greater \[optional\]
- wolfssl 5.5.0 or greater \[optional\]
- quiche 0.22.0 or greater \[optional\]
- tquic 1.5.0 or greater \[optional\]

### For Arch Linux
```bash
pacman -Sy libev
```

```bash
# optinal
pacman -Sy gmp openssl zlib libevdev curl
```

or 

```bash
paru -Sy libev
```

```bash
paru -Sy gmp openssl zlib libevdev curl
```

### For Windows

No support

### For MacOs

No support

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
    auto ctx = manapi::async::context::create();
    auto db = std::make_shared<manapi::ext::pq::connection>(ctx);
    manapi::net::http::server router (ctx) 

    ctx->eventloop()->setup_handle_interrupt();

    router.config_object({
        {"pools", manapi::json::array({
            {
                {"address", "127.0.0.1"},
                {"http_versions", ["2", "1.1"]},
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

    router.GET ("/", [cnt = std::make_shared<std::atomic<int>>(0)] (decltype(router)::req req, decltype(router)::resp resp) mutable -> manapi::future<> {
        co_return resp.text(std::format("Hello World! Count: {}", cnt->fetch_add(1)));
    });

    router.GET("/+error", [](decltype(router)::req req, decltype(router):::resp resp) -> manapi::future<> {
        resp.replacers({
            {"status_code", std::to_string(resp.status_code())},
            {"status_message", std::string{resp.status_message()}}
        });

        co_return resp.file ("../examples/error.html");
    });

    router.POST("/+error", [](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        co_return resp.json({{"error", resp.status_code()},
                {"msg", std::string{resp.status_message()}}});
    });

    router.GET("/cat", [&ctx](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        auto fetch = co_await manapi::net::fetch2::fetch (ctx, "https://dragonball-api.com/api/planets/7", {
            {"enable_ssl_verify", false},
            {"enable_alpn", true},
            {"method", "GET"}
        });

        if (!fetch->ok()) {
            co_return resp.json ({{"error", true}, {"message", "fetch failed"}});
        }

        auto data = co_await fetch->json();

        co_return resp.text(std::move(data["description"].as_string()));
    });

    router.GET("/pq", [db, mx = std::make_shared<manapi::async::mutex>(ctx)](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        auto lk = co_await mx->lock_guard();
        /* The pool of database connections here / That example is so slow */
        auto res = co_await db->exec("SELECT id, str_col FROM for_test WHERE id > $1", 0);

        lk.call();

        std::string content;
        for (const auto &row: res) {
            content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
        }

        co_return resp.text(std::move(content));
    });

    router.GET("/proxy", [](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        co_return resp.proxy("http://127.0.0.1:8889/video");
    });

    router.GET("/video", [](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        resp.partial_status(true);
        resp.compress_enabled(false);
        co_return resp.file("video.mp4");
    });

    router.GET("/stop", [ctx](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        /* stop the app */
        co_await ctx->stop();
        co_return resp.text("stopped");
    });

    router.GET("/timeout", [ctx](decltype(router)::req req, decltype(router)::resp resp) -> manapi::future<> {
        /* stop the app */
        co_await manapi::async::delay{ctx, 10000};
        co_return resp.text("10sec");
    });

    manapi::async::run(ctx, [router, db] () -> manapi::future<> {
        co_await db->connect("address", "port", "username", "password", "db");
        co_await router.start();
    });

    ctx->sync_start();

    return 0;
}
```

## Tested
- Tested on Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland