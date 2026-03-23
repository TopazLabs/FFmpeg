from conan import ConanFile
from conan.tools.files import copy
import os

# NOTE: when updating, place libraries in alphabetical order to reduce diffs


class conanRecipe(ConanFile):
    name = "FFmpeg"
    # version
    settings = "os", "build_type", "arch"
    options = {
        "build_linux_GPL": [True, False]
    }
    default_options = {
        "build_linux_GPL": False
    }

    def configure(self):
        self.options["zimg"].shared = True
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.options["libvpx"].shared = True
        if self.settings.os == "Macos":
            self.options["openssl"].shared = False
        
        if self.settings.os == "Windows" and self.settings.arch == "x86_64":
            self.options["libaom-av1"].shared = True

        if self.settings.os == "Linux" and self.options.build_linux_GPL:
            self.options["libx265"].shared = True
            self.options["libx264"].shared = True

    def build_requirements(self):
        if self.settings.os == "Macos" and self.settings.arch == "x86_64":
            self.tool_requires("nasm/2.16.01")
        if self.settings.os == "Windows":
            self.tool_requires("nasm/2.16.01")

    # windows libaom-av1 build different recipe revision id for some reason...
    def requirements(self):
        self.requires("videoai/[~2.0.0]")
        self.requires("libvpx/1.14.1")
        if self.settings.os == "Macos" and self.settings.arch == "x86_64":
            self.requires("zimg/3.0.5@josh/oiio3")
        else:
            self.requires("zimg/3.0.5")
        if self.settings.os == "Windows":
            self.requires("libaom-av1/3.5.0#041e72afabd2cb62567a667c7f9ed08a")
            self.requires("amf/1.4.36")
            self.requires("libvpl/2025.4.18")
            self.requires("zlib-mt/1.2.13")
        else:
            self.requires("libaom-av1/3.5.0#0e3100f015c5c5fab8e10ab07c566c53")

        if self.settings.os == "Linux" and self.options.build_linux_GPL:
            self.requires("libx264/cci.20240224")
            self.requires("libx265/3.4")

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
