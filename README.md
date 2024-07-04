# Manapi HTTP Server/Client

## Introduction
HTTP server written on C++ which support HTTP/1.1, HTTP/2 and HTTP/3

> [!CAUTION]
> This project in development!!!

## About
This HTTP server need to make easy develop `web applications`, `API-interfaces` and other things.

So many important utils will be supported out of the box, for example, `JSON`, `MySQL-client`, `PostgreSQL-client`, `JSON-masks`, `Big Int`, `modules`, `plugins`.

## Installation
For compile this project, you need to install below projects:
- OpenSSL 3.3.1 or greater
- zlib 1.3.1 or greater
- quiche 0.21.0 or greater
- gmp 6.3.0 or greater
- libev 4.33-3 or greater

### For Arch Linux
```bash
pacman -S gmp openssl libev quiche zlib libevdev
```

or 

```bash
paru -S gmp openssl libev quiche zlib libevdev
```

## Example

```c++
int main ()
{
    std::atomic<bool> flag = false;
    
    http server;
    
    server.set_config ("config.json");
    
    server.GET ("/", "/static/files");
    
    server.GET ("/+error", [](REQ(req), RESP(resp)) {
        std::map <std::string, std::string> replacers = {
            {"status_code", std::to_string(resp.get_status_code())},
            {"status_message", resp.get_status_message()}
        };
        resp.set_replacers (replacers);
        resp.file ("/error.html");
    });
    
    server.POST ("/api/+error", [](REQ(req), RESP(resp)) {
        resp.text (R"({ "error": true, "message": "An error has occurred"})");
    });
    
    server.POST ("/api/[key]/form", [&flag](REQ(req), RESP(resp)) {
        if (*req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }
        
        json jp = json::object();
        jp.insert ("flag", flag ? "yes" : "no");
        
        auto formData = req.form();
        
        for (auto &item: formData)
        {
            jp.insert(item.first, item.second);
        }
        
        resp.text (jp.dump(4));
    });
    
    server.GET ("/api/[key]/toggle", [&flag](REQ(req), RESP(resp)) {
        if (*req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }
        
        flag = !flag;
        
        resp.text ("ok");
    });

    auto f = server.run();
    
    f.get();
    
    return 0;
}

```

## Tested
- Tested on Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland