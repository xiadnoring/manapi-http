from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, cmake_layout, CMakeToolchain

class ManapiHttpConan(ConanFile):
    name = "manapihttp"
    description = "Fast http server/client"
    version = "0.0.1"

    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False]
    }

    default_options = {"shared": False, "fPIC": True}

    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

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

    def requirements(self):
        self.requires("libev/4.33")
        self.requires("openssl/3.2.2")
        self.requires("zlib/1.3.1")
        self.requires("gmp/6.3.0")
        self.requires("quiche/0.22.0")
        self.requires("libcurl/8.6.0")

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_file_name", "manapihttp")
        self.cpp_info.set_property("cmake_target_name", "manapihttp::manapihttp")
        self.cpp_info.set_property("pkg_config_name", "manapihttp")

        self.cpp_info.libs = ["manapihttp"]