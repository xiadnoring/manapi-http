from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, cmake_layout, CMakeToolchain
from conan.tools.apple import fix_apple_shared_install_name
from conan.errors import ConanInvalidConfiguration
from conan.tools.env import VirtualBuildEnv, VirtualRunEnv

class ManapiHttpConan(ConanFile):
    name = "manapihttp"
    description = "Fast http server/client"
    version = "1.0.0"

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
        "zstd_dependency": [True, False],
        "brotli_dependency": [True, False],
        "grpc_dependency": [True, False],
        "nghttp2_dependency": [True, False],
        "nghttp3_dependency": [True, False],
        "cpptrace_dependency": [True, False],
        "lib": [True, False]
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "json_debug": True,
        "wolfssl_dependency": False,
        "openssl_dependency": True,
        "quiche_dependency": True,
        "tquic_dependency": False,
        "lib": False,
        "curl_dependency": True,
        "gmp_dependency": True,
        "zlib_dependency": True,
        "zstd_dependency": True,
        "brotli_dependency": True,
        "grpc_dependency": False,
        "nghttp2_dependency": True,
        "nghttp3_dependency": True,
        "cpptrace_dependency": False
    }

    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt", "preprocess/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

        if self.options.get_safe('wolfssl_dependency', False):
            self.options["wolfssl"].alpn = True
            self.options["wolfssl"].sslv3 = True
            self.options["wolfssl"].tls13 = True
            self.options["wolfssl"].with_quic = True

            if not self.options.get_safe('openssl_dependency', False):
                self.options["wolfssl"].opensslextra = True

                if self.options.get_safe('curl_dependency', False):
                    self.options["wolfssl"].with_curl = True
                    self.options["libcurl"].with_ssl = 'wolfssl'

        if self.settings.os != 'Windows' and not self.options.get_safe("lib", False):
            self.options["wolfssl"].shared = True
            self.options["openssl"].shared = True
            self.options["libev"].shared = True
            self.options["quiche"].shared = True
            self.options["tquic"].shared = True
            self.options["zlib"].shared = True
            self.options["libcurl"].shared = True
            self.options["gmp"].shared = self.settings.compiler != 'msvc'
            self.options["libpq"].shared = True
            self.options["cpptrace"].shared = True
            self.options["brotli"].shared = True
            self.options["zstd"].shared = self.settings.compiler != 'msvc'
            self.options["grpc"].shared = True
            self.options["nghttp2"].shared = True
            self.options["nghttp3"].shared = True
            self.options["kainjow_mustache"].shared = True

        self.options["libcurl"].with_nghttp2 = True
        self.options["quiche"].shared = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        VirtualBuildEnv(self).generate()

        runenv = VirtualRunEnv(self)
        runenv.generate()

        tc = CMakeToolchain(self)
        tc.variables['MANAPIHTTP_BUILD_METHOD'] = "conan"
        tc.variables['MANAPIHTTP_JSON_DEBUG'] = self.options.get_safe('json_debug', False)
        tc.variables['MANAPIHTTP_BROTLI_DEPENDENCY'] = self.options.get_safe('brotli_dependency', False)
        tc.variables['MANAPIHTTP_ZSTD_DEPENDENCY'] =self.options.get_safe('zstd_dependency', False)
        tc.variables['MANAPIHTTP_WOLFSSL_DEPENDENCY'] = self.options.get_safe('wolfssl_dependency', False)
        tc.variables['MANAPIHTTP_OPENSSL_DEPENDENCY'] = self.options.get_safe('openssl_dependency', False)
        tc.variables['MANAPIHTTP_QUICHE_DEPENDENCY'] = self.options.get_safe('quiche_dependency', False)
        tc.variables['MANAPIHTTP_TQUIC_DEPENDENCY'] = self.options.get_safe('tquic_dependency', False)
        tc.variables['MANAPIHTTP_CURL_DEPENDENCY'] = self.options.get_safe('curl_dependency', False)
        tc.variables['MANAPIHTTP_GMP_DEPENDENCY'] = self.options.get_safe('gmp_dependency', False)
        tc.variables['MANAPIHTTP_ZLIB_DEPENDENCY'] = self.options.get_safe('zlib_dependency', False)
        tc.variables['MANAPIHTTP_MSQUIC_DEPENDENCY'] = self.options.get_safe('msquic_dependency', False)
        tc.variables['MANAPIHTTP_GRPC_DEPENDENCY'] = self.options.get_safe('grpc_dependency', False)
        tc.variables['MANAPIHTTP_NGHTTP2_DEPENDENCY'] = self.options.get_safe('nghttp2_dependency', False)
        tc.variables['MANAPIHTTP_NGHTTP3_DEPENDENCY'] = self.options.get_safe('nghttp3_dependency', False)
        tc.variables['MANAPIHTTP_CPPTRACE_DEPENDENCY'] = self.options.get_safe('cpptrace_dependency', False)
        tc.variables['MANAPIHTTP_BUILD_TYPE'] = 'lib' if self.options.get_safe('lib', False) else 'exe'
        tc.cache_variables["CMAKE_TRY_COMPILE_CONFIGURATION"] = str(self.settings.build_type)
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def validate(self):
        check_min_cppstd(self, 23)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def build_requirements(self):
        if self.options.get_safe('grpc_dependency', False):
            self.tool_requires("protobuf/<host_version>")

    def package(self):
        cmake = CMake(self)
        cmake.install()

        fix_apple_shared_install_name(self)

    def requirements(self):
        self.requires("libuv/1.49.2")

        if self.options.get_safe('brotli_dependency', False):
            self.requires("brotli/[>=1.1.0 <2]")

        if self.options.get_safe('zstd_dependency', False):
            self.requires("zstd/[>=1.5.5 <1.5.7]")

        if self.options.get_safe('grpc_dependency', False):
            self.requires("grpc/[>=1.72.0 <2]")
            self.requires("protobuf/[>=4.25.3 <6]")

        if not self.options.get_safe('lib', False):
            self.requires("libpq/16.8")

        if self.options.get_safe('cpptrace_dependency', False):
            self.requires("cpptrace/[>=0.7.4 <1]")

        if self.options.get_safe('zlib_dependency', False):
            self.requires("zlib/1.3.1")

        if self.options.get_safe('gmp_dependency', False):
            self.requires("gmp/6.3.0")

        if self.options.get_safe('curl_dependency', False):
            self.requires("libcurl/[>=8.12.1 <9]")

        if self.options.get_safe('openssl_dependency', False):
            self.requires("openssl/[>=3.5.1 <4]")

        if self.options.get_safe('wolfssl_dependency', False):
            self.requires("wolfssl/[>=5.0.0]")

        if self.options.get_safe('quiche_dependency', False):
            self.requires("quiche/[>=0.24.0]")

        if self.options.get_safe('tquic_dependency', False):
            self.requires("tquic/[>=1.6.0]")

        if self.options.get_safe('nghttp2_dependency', False):
            self.requires("libnghttp2/[>=1.59.0 <2]")

        if self.options.get_safe('nghttp3_dependency', False):
            self.requires("nghttp3/[>=1.6.0 <2]")

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