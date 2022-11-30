from conans import ConanFile, tools

class conanRecipe(ConanFile):
	name = "topaz-ffmpeg"
	settings = ("os", "build_type", "arch")

	def requirements(self):
		self.requires("videoai/[~0.9.0]")
		if self.settings.os == "Macos" or self.settings.os == "Linux":
		    self.requires("libvpx/1.11.0") #libvpx is static on Windows
		    self.requires("aom/3.5.0")

	def imports(self):
		if self.settings.os == "Windows":
			self.copy("*")
		if self.settings.os == "Macos":
			self.copy("*")
		if self.settings.os == "Linux":
			self.copy("*")
