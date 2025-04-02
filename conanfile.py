from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, cmake_layout, CMakeToolchain
from conan.tools.apple import fix_apple_shared_install_name
from conan.errors import ConanInvalidConfiguration

class ManapiHttpConan(ConanFile):
    name = "manapihttp"
    description = "Fast http server/client"
    version = "0.0.2"

    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "json_debug": [True, False],
        "openssl_dependency": [True, False],
        "wolfssl_dependency": [True, False],
        "quiche_dependency": [True, False],
        "tquic_dependency": [True, False],
        "zlib_dependency": [True, False],
        "curl_dependency": [True, False],
        "gmp_dependency": [True, False],
        "lib": [True, False]
    }

    default_options = {"shared": False, "fPIC": True, "json_debug": True, "wolfssl_dependency": True, "openssl_dependency": False, "quiche_dependency": True,
                       "tquic_dependency": True, "lib": True, "curl_dependency": True, "gmp_dependency": True, "zlib_dependency": True}

    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt", "preprocess/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

        if self.options.get_safe('wolfssl_dependency', False):
            self.options["wolfssl/*"].alpn = True
            self.options["wolfssl/*"].sslv3 = True
            self.options["wolfssl/*"].tls13 = True

            if self.options.get_safe("openssl_dependency", False):
                raise ConanInvalidConfiguration("OpenSSL has conflicts with WolfSSL")

        if self.options.shared:
            if self.options.get_safe('openssl_dependency', False):
                self.options["openssl/*"].shared = True

            if self.options.get_safe('wolfssl_dependency', False):
                self.options["wolfssl/*"].shared = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables['MANAPIHTTP_BUILD_METHOD'] = "conan"
        tc.variables['MANAPIHTTP_JSON_DEBUG'] = self.options.get_safe('json_debug', False)
        tc.variables['MANAPIHTTP_WOLFSSL_DEPENDENCY'] = self.options.get_safe('wolfssl_dependency', False)
        tc.variables['MANAPIHTTP_OPENSSL_DEPENDENCY'] = self.options.get_safe('openssl_dependency', False)
        tc.variables['MANAPIHTTP_QUICHE_DEPENDENCY'] = self.options.get_safe('quiche_dependency', False)
        tc.variables['MANAPIHTTP_TQUIC_DEPENDENCY'] = self.options.get_safe('tquic_dependency', False)
        tc.variables['MANAPIHTTP_BUILD_TYPE'] = 'lib' if self.options.get_safe('lib', False) else 'exe'
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
        self.requires("libcurl/[>=8.12.1 <9]")
        self.requires("libpq/15.5")
        self.requires("cpptrace/0.7.4")
        # self.requires("mimalloc/2.1.7")
        # self.requires("jemalloc/5.3.0")

        if self.options.get_safe('openssl_dependency', False):
            self.requires("openssl/[>=3.3.2 <4]")

        if self.options.get_safe('wolfssl_dependency', False):
            self.requires("wolfssl/[>=5.7.2]")

        if self.options.get_safe('quiche_dependency', False):
            self.requires("quiche/[>=0.23.4]")

        if self.options.get_safe('tquic_dependency', False):
            self.requires("tquic/[>=1.6.0]")

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