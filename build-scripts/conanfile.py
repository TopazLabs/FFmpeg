from conan import ConanFile
from conan.tools.files import copy
import os

# NOTE: when updating, place libraries in alphabetical order to reduce diffs


class conanRecipe(ConanFile):
    name = "FFmpeg"
    # version
    settings = "os", "build_type", "arch"

    def configure(self):
        self.options["zimg"].shared = True
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.options["libvpx"].shared = True

    def build_requirements(self):
        if self.settings.os == "Macos" and self.settings.arch == "x86_64":
            self.tool_requires("nasm/2.14")
        if self.settings.os == "Windows":
            self.tool_requires("yasm/1.3.0")

    def requirements(self):
        self.requires("videoai/[~1.4.0]")
        self.requires("libvpx/1.11.0")
        self.requires("aom/3.5.0")
        self.requires("zimg/3.0.5")
        if self.settings.os == "Windows":
            self.requires("amf/1.4.33")
            self.requires("libvpl/2023.0.0")
            self.requires("zlib-mt/1.2.13")

    def generate(self):
        for dep in self.dependencies.values():
            if dep.package_folder:
                print(f"copying {dep}: {dep.package_folder} -> {self.build_folder}")
                if self.settings.os == "Windows":
                    # Copy all the libraries to lib3rdparty
                    # Cannot only grab specific types, because for some reason
                    # tensorflow-gpu has c++ headers with no extension
                    copy(self, "*", src=dep.package_folder, dst=os.path.join("lib3rdparty", str(dep.ref).split('/')[0]))
                    # Copy DLLs and Crashpad executables to bin folder
                    copy(self, "*.xml", src=os.path.join(dep.package_folder, "bin"), dst="bin")
                    copy(self, "*.dll", src=os.path.join(dep.package_folder, "bin"), dst="bin")
                    # Copy DLLs and other things from older pre-builts that use binr/bind
                    copy(self, "*", dst="bin", src=os.path.join(dep.package_folder, "binr"))

                if self.settings.os == "Macos":
                    copy(self, "*", src=os.path.join(dep.package_folder, "include"), dst="include")
                    copy(self, "*", src=os.path.join(dep.package_folder, "lib"), dst="lib")
                if self.settings.os == "Linux":
                    copy(self, "*", src=os.path.join(dep.package_folder, "include"), dst="include")
                    copy(self, "*", src=os.path.join(dep.package_folder, "lib"), dst="lib")
