from conan import ConanFile
from conan.tools.files import copy, collect_libs
import os

class conanRecipe(ConanFile):
    name = "topaz-ffmpeg"
    settings = "os", "build_type", "arch"

    def configure(self):
        self.options["zimg"].shared = True
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.options["libvpx"].shared = True
        if self.settings.os == "Macos":
            self.options["openssl"].shared = False

        if self.settings.os == "Windows" and self.settings.arch == "x86_64":
            self.options["libaom-av1"].shared = True

    def requirements(self):
        self.requires("videoai/[~2.0.0]")
        if self.settings.os == "Macos" and self.settings.arch == "x86_64":
            self.requires("zimg/3.0.5@josh/oiio3")
        else:
            self.requires("zimg/3.0.5")
        
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.requires("libvpx/1.14.1") #libvpx is static on Windows
            self.requires("libaom-av1/3.5.0#0e3100f015c5c5fab8e10ab07c566c53")
        else:
            self.requires("libaom-av1/3.5.0#041e72afabd2cb62567a667c7f9ed08a")
            
    def package_id(self):
        self.info.requires["videoai"].minor_mode()

    def package(self):

        if self.settings.os=="Windows":
            copy(
                self,
                "*",
                src=self.source_folder,
                dst=self.package_folder,
                keep_path=True,
                excludes=["*.lib", "*.def"]
            )
            copy(
                self,
                "*.lib",
                src=self.source_folder,
                dst=os.path.join(self.package_folder, "lib"),
                keep_path=False,
            )
        else:
            copy(
                self,
                "*",
                src=self.source_folder,
                dst=self.package_folder,
                keep_path=True,
            )

    def package_info(self):
        # Define individual components for each FFmpeg library
        
        # avutil - core utility library (base for others)
        self.cpp_info.components["avutil"].libs = ["avutil"]
        
        # avcodec - codec library
        self.cpp_info.components["avcodec"].libs = ["avcodec"]
        self.cpp_info.components["avcodec"].requires = ["avutil"]
        
        # avformat - format library
        self.cpp_info.components["avformat"].libs = ["avformat"]
        self.cpp_info.components["avformat"].requires = ["avutil", "avcodec"]
        
        # avfilter - filter library
        self.cpp_info.components["avfilter"].libs = ["avfilter"]
        self.cpp_info.components["avfilter"].requires = ["avutil"]
        
        # avdevice - device library
        self.cpp_info.components["avdevice"].libs = ["avdevice"]
        self.cpp_info.components["avdevice"].requires = ["avutil", "avformat"]
        
        # swscale - scaling library
        self.cpp_info.components["swscale"].libs = ["swscale"]
        self.cpp_info.components["swscale"].requires = ["avutil"]
        
        # swresample - resampling library
        self.cpp_info.components["swresample"].libs = ["swresample"]
        self.cpp_info.components["swresample"].requires = ["avutil"]

    def layout(self):
        folder_dir = self.conf.get("user.path:folder_dir", default=None)
        if not folder_dir:
            folder_dir = f"{self.settings.os}_{self.settings.arch}".lower()

        self.output.info(f"folder_dir: {folder_dir}")
        self.folders.source = folder_dir
        
        self.cpp.package.libs = collect_libs(self)
