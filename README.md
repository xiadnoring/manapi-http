# Manapi HTTP Server/Client

## Introduction
HTTP server written on C++ 2x which support HTTP/1.1 and HTTP/3

> [!CAUTION]
> This project in development!!!

## About
This HTTP server need to make easy develop `web applications`, `API-interfaces` and other things.

So many important utils will be supported out the box, for example, `JSON`, `MySQL-client`, `PostgreSQL-client`, `JSON-masks`, `Big Int`, `modules`, `plugins`.

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

## Tested
- Tested on Hyprland Arch Linux x86_64 kernel 6.9.3-zen1-1-zen wayland