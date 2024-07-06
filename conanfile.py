from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps

class manapihttpRecipe (ConanFile):
    name = "manapihttp"
    version = "0.0.1"

    # Metadata
    license = "Apache-2.0"
    author = "Xiadnoring"
    url = "https://github.com/xiadnoring/manapi-http"
    description = "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications"
    topics = ("http3", "http", "server", "C++")

    # Binary Configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": False}

    # Sources
    exports_sources = "CMakeLists.txt", "src/*", "includes/*"

    def config_options(self):
        pass

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()


    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["manapihttp"]