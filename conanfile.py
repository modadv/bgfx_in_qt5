from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class BgfxInQtConan(ConanFile):

    name = "bgfx_in_qt"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "with_tests": [True, False],
        "use_system_qt": [True, False],
    }

    default_options = {
        "with_tests": True,
        "use_system_qt": False,
        "qt/*:shared": True,
        "qt/*:qtquickcontrols": True,
        "qt/*:with_sqlite3": False,
    }

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["BUILD_TESTING"] = bool(self.options.with_tests)
        if self.settings.os == "Linux":
            toolchain.variables["CMAKE_CXX_FLAGS"] = "-Wno-error=deprecated-declarations"
        self._configure_system_qt(toolchain)
        toolchain.generate()
        CMakeDeps(self).generate()

    def requirements(self):
        if not self.options.use_system_qt:
            self.requires("qt/5.15.11", options={"with_sqlite3": False})
        self.requires("nlohmann_json/3.11.3")

    def _configure_system_qt(self, toolchain):
        if not self.options.use_system_qt:
            return

        qt_prefix = self.conf.get("user.build:system_qt_prefix")
        if qt_prefix:
            toolchain.cache_variables["CMAKE_PREFIX_PATH"] = qt_prefix
