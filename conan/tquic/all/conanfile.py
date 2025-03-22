from conan import ConanFile
from conan.tools.files import files, get, copy
from conan.tools.scm import Git
from conan.errors import ConanInvalidConfiguration
from conan.tools.system.package_manager import Apt, PacMan, Brew, Zypper, Yum, Dnf, Apk, Chocolatey

import os
import subprocess
import glob
import shutil

required_conan_version = ">=2.0.0"

class QuciheConan(ConanFile):
    name = "tquic"
    description = "A high-performance, lightweight, and cross-platform QUIC library"
    url = "https://github.com/Tencent/tquic"

    license = "BSD-2-Clause"
    author = "Tencent, Inc"
    topics = ("tquic", "rust", "quic", "http3")

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False], 'FFI': [True, False]}
    default_options = {"shared": False, "fPIC": True, 'FFI': True}

    package_type = "library"

    def system_requirements(self):
        PacMan(self).install(["rust"], update=True, check=True)
        Brew(self).install(["rustup"], update=True, check=True)
        Zypper(self).install(["cargo"], update=True, check=True)
        Yum(self).install(["rust-toolset"], update=True, check=True)
        Dnf(self).install(["rust-toolset"], update=True, check=True)
        Apk(self).install(["rust"], update=True, check=True)
        Chocolatey(self).install(["rustup.install"], update=True, check=True)

        apt = Apt(self)
        apt.update()

        try:
            apt.install ("rustup", check=True)
            return
        except:
            print ("Can not install `rustup` deb package")
        
        try:
            apt.install ("cargo", check=True)
            return
        except:
            print ("Can not install `cargo` deb package")
        
        try:
            apt.install ("rustc", check=True)
            return
        except:
            print ("Can not install `cargo` deb package")

        raise ConanInvalidConfiguration ("Can not install Rust")
            
    def command_exists(self, command):
        cmd = "which"

        if self.settings.os == "Windows":
            cmd = "where"

        return subprocess.call([cmd, command]) == 0

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=False)

        if os.path.exists('src'):
            shutil.rmtree('src')
        files.rename(self, glob.glob('tquic*')[0], 'src')
        boringssl = os.path.join('src', 'deps', 'boringssl')

        if len(os.listdir(boringssl)) == 0:
            # boringssl not exists! Lets fix that!
            get(self, 'https://github.com/google/boringssl/archive/refs/tags/0.20241209.0.zip', destination=boringssl, strip_root=True)

    def generate(self):
        if not self.command_exists('cargo'):
            raise ConanInvalidConfiguration ("This project made on rust, so the OS must provide the 'cargo'. Can not find the 'cargo' (which cargo)")

    def build(self):
        args = ['cargo', 'build', '--manifest-path', os.path.join('src', 'Cargo.toml'), '--lib', '--release']

        if self.options['FFI']:
            args.append ('--features')
            args.append ('ffi')

        result = subprocess.call (args)

        if result != 0:
            raise ConanInvalidConfiguration ("Cargo sent an invalid code. can not compile a src")

    def package_id(self):
        self.info.clear()
        
    def package(self):
        release = os.path.join ('src', 'target', 'release')

        libpkg = os.path.join (self.package_folder, 'lib')
        includepkg = os.path.join (self.package_folder, 'include')

        if not os.path.exists(libpkg):
            os.mkdir (libpkg)

        if not os.path.exists(includepkg):
            os.mkdir (includepkg)

        include = os.path.join ('src', 'include')

        # lib*
        for item in glob.glob (os.path.join (release, 'lib*')):
            result = files.shutil.copy (item, libpkg)

            if len(item) > 3 and item[-3:] == '.so':
                # workaround
                os.symlink (result, os.path.join (libpkg, os.path.basename(item) + '.0'))

        for item in glob.glob (os.path.join (release, '*dll')):
            files.shutil.copy (item, libpkg)

        files.shutil.copy (os.path.join(include, 'tquic.h'), includepkg)
        files.shutil.copy (os.path.join(include, 'tquic_def.h'), includepkg)

        # License
        copy (self, 'COPYING', src=os.path.join(self.source_folder, 'src'), dst=os.path.join(self.package_folder, 'licenses', 'tquic'))
        copy (self, 'LICENSE', src=os.path.join(self.source_folder, 'src', 'deps', 'boringssl'), dst=os.path.join(self.package_folder, 'licenses', 'boringssl'))
    def package_info(self):
        self.cpp_info.set_property ("cmake_find_mode", "both")
        self.cpp_info.set_property ("cmake_file_name", "tquic")
        self.cpp_info.set_property ("cmake_target_name", "tquic::tquic")
        self.cpp_info.set_property ("pkg_config_name", "tquic")
        self.cpp_info.libs = ["tquic"]

