from conan import ConanFile
from conan.tools.files import copy, collect_libs

class conanRecipe(ConanFile):
    name = "topaz-ffmpeg"
    settings = "os", "build_type", "arch"

    def configure(self):
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.options["libvpx"].shared = True

    def requirements(self):
        self.requires("videoai/[~1.1.0]")
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.requires("libvpx/1.11.0") #libvpx is static on Windows
            self.requires("aom/3.5.0")
            
    def package_id(self):
        self.info.requires["videoai"].minor_mode()

    def package(self):
        copy(
            self,
            "*",
            src=self.source_folder,
            dst=self.package_folder,
            keep_path=True,
        )

    def layout(self):
        self.folders.source = self.conf.get("user.profile_name")
        self.cpp.package.libs = collect_libs(self)
