from conans import ConanFile, tools

# NOTE: when updating, place libraries in alphabetical order to reduce diffs

class conanRecipe(ConanFile):
    name = "FFmpeg"
    #version
    settings = ("os", "build_type", "arch")

    def configure(self):
        if self.settings.os == "Macos" or self.settings.os == "Linux":
            self.options["libvpx"].shared = True


    def requirements(self):
        self.requires("videoai/[~0.9.0]")
        self.requires("libvpx/1.11.0")
        self.requires("aom/3.5.0")
        if self.settings.os == "Macos":
            if self.settings.arch == "x86_64":
                self.requires("nasm/2.14")

    def imports(self):
        if self.settings.os == "Windows":
            self.copy("*", "lib3rdparty", folder=True)
            self.copy("*", "bin", "bin")
            self.copy("*", "bin", "binr")
            # self.copy('*', src='@bindirs', dst='binr')
        if self.settings.os == "Macos":
            self.copy("*", "include", "include")
            self.copy("*", "lib", "lib")
            self.copy("nasm", "bin", "bin")
        if self.settings.os == "Linux":
            self.copy("*", "include", "include")
            self.copy("*", "lib", "lib")
