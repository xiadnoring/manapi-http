from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, cmake_layout, CMakeToolchain
from conan.tools.apple import fix_apple_shared_install_name

class ManapiHttpConan(ConanFile):
    name = "manapihttp"
    description = "Fast http server/client"
    version = "0.0.1"

    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "json-debug": [True, False],
        "openssl-dependency": [True, False],
        "wolfssl-dependency": [True, False],
        "quiche-dependency": [True, False],
        "tquic-dependency": [True, False]
    }

    default_options = {"shared": False, "fPIC": True, "json-debug": True, "wolfssl-dependency": True, "openssl-dependency": True, "quiche-dependency": True,
                       "tquic-dependency": True}

    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt", "preprocess/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

        if self.options.shared:
            if self.options.get_safe('openssl-dependency', False):
                self.options["openssl/*"].shared = True

            if self.options.get_safe('wolfssl-dependency', False):
                self.options["wolfssl/*"].shared = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)

        tc.variables['MANAPIHTTP_JSON_DEBUG'] = self.options.get_safe('json-debug', False)
        tc.variables['MANAPIHTTP_WOLFSSL_DEPENDENCY'] = self.options.get_safe('wolfssl-dependency', False)
        tc.variables['MANAPIHTTP_OPENSSL_DEPENDENCY'] = self.options.get_safe('openssl-dependency', False)
        tc.variables['MANAPIHTTP_QUICHE_DEPENDENCY'] = self.options.get_safe('quiche-dependency', False)
        tc.variables['MANAPIHTTP_TQUIC_DEPENDENCY'] = self.options.get_safe('tquic-dependency', False)

        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

        fix_apple_shared_install_name(self)

    def requirements(self):
        self.requires("libev/4.33")
        self.requires("zlib/1.3.1")
        self.requires("gmp/6.3.0")
        self.requires("libcurl/8.6.0")

        if self.options.get_safe('openssl-dependency', False):
            self.requires("openssl/3.3.2")

        if self.options.get_safe('wolfssl-dependency', False):
            self.requires("wolfssl/5.7.2")

        if self.options.get_safe('quiche-dependency', False):
            self.requires("quiche/0.22.0")

        if self.options.get_safe('tquic-dependency', False):
            self.requires("tquic/1.5.0")

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_file_name", "manapihttp")
        self.cpp_info.set_property("cmake_target_name", "manapihttp::manapihttp")
        self.cpp_info.set_property("pkg_config_name", "manapihttp")

        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs = ["dl", "m", "pthread"]
        elif self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32", "shlwapi"]

        self.cpp_info.libs = ["manapihttp"]