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
- OpenSSL 3.3.1 or greater
- zlib 1.3.1 or greater
- gmp 6.3.0 or greater
- libev 4.33-3 or greater
- curl 8.8.0-1 or greater

### For Arch Linux
```bash
pacman -S gmp openssl libev zlib libevdev curl
```

or 

```bash
paru -S gmp openssl libev zlib libevdev curl
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
int main ()
{
    std::atomic<bool> flag = false;
    
    auto ctx = manapi::async::context::create();
    ctx->eventloop()->setup_handle_interrupt();
    
    http server (ctx);
    
    server.set_config ("config.json");
    
    server.GET ("/", "/static/files");
    
    server.GET ("/+error", [](REQ(req), RESP(resp)) -> future<void> {
        std::map <std::string, std::string> replacers = {
            {"status_code", std::to_string(resp.get_status_code())},
            {"status_message", resp.get_status_message()}
        };
        resp.set_replacers (replacers);
        resp.file ("/error.html");
        co_return;
    });
    
    server.POST ("/api/+error", [](REQ(req), RESP(resp)) -> future<void>  {
        resp.json ({
            {"error", true},
            {"message", "An error has occurred"}
        });
        co_return;
    });
    
    {
        const json_mask post_mask = {
            {"id", "{string(<=2)|integer(>=0 <100)}"}
            {"first-name", "{string(>=5 <70)}"},
            {"last-name", "{string(>=5 <70)}"},
            {"age", "{integer(>=18 <100)}"},
            {"tags", "{string(>=3 <10)[<10]}"}
        };
        
        server.POST ("/api/[key]/form", [&flag](REQ(req), RESP(resp)) -> future<void>  {
            if (req.get_param("key") != "123")
            {
                throw std::runtime_error ("bad key");
            }
            
            json data = {
                {"flag", flag ? "yes" : "no"}
            };
            
            auto formData = co_await req.form();
            
            for (auto &item: formData)
            {
                jp.insert(item.first, item.second);
            }
            
            resp.json(data, 4);
            co_return;
        }, nullptr, post_mask);
    }
    
    server.GET ("/api/[key]/toggle", [&flag](REQ(req), RESP(resp)) -> future<void>  {
        if (req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }
        
        flag.store(!flag.load());
        
        resp.text ("ok");
        co_return;
    });

    manapi::async::run (ctx, server.start());
    ctx->sync_start();
    
    return 0;
}

```

## Tested
- Tested on Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland