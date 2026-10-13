"""Builds the nlu_script shared library with CMake and packages it with the
ctypes binding: `pip install ./python`."""
import glob
import os
import shutil
import subprocess
import sys

from setuptools import setup
from setuptools.command.build_py import build_py
from setuptools.dist import Distribution

try:
    from setuptools.command.bdist_wheel import bdist_wheel
except ImportError:  # setuptools < 70.1
    from wheel.bdist_wheel import bdist_wheel

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class BuildPyWithLibrary(build_py):
    def run(self):
        build_py.run(self)
        build_dir = os.path.join(HERE, "build", "cmake")
        configure = ["cmake", "-S", ROOT, "-B", build_dir,
                     "-DCMAKE_BUILD_TYPE=Release", "-DNLU_SCRIPT_BUILD_TESTS=OFF"]
        if sys.platform == "win32":
            # Link the C++ runtime statically so the DLL doesn't need the
            # Visual C++ Redistributable (Python ships vcruntime, not msvcp).
            configure += ["-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
                          "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
        subprocess.check_call(configure)
        subprocess.check_call(["cmake", "--build", build_dir, "--target", "nlu_script",
                               "--config", "Release"])
        patterns = ["libnlu_script.so", "libnlu_script.dylib", "nlu_script.dll"]
        built = [path for pattern in patterns
                 for path in glob.glob(os.path.join(build_dir, "**", pattern), recursive=True)]
        if not built:
            raise RuntimeError("CMake did not produce the nlu_script shared library")
        target = os.path.join(self.build_lib, "nlu_script")
        os.makedirs(target, exist_ok=True)
        shutil.copy(built[0], target)


class BinaryDistribution(Distribution):
    """The package ships a native library, so wheels are platform specific."""

    def has_ext_modules(self):
        return True


class BdistWheelAnyPython(bdist_wheel):
    """The library is loaded with ctypes, so one wheel per platform serves
    every Python 3 version: tag it py3-none-<platform>."""

    def get_tag(self):
        return ("py3", "none", bdist_wheel.get_tag(self)[2])


setup(
    name="nlu_script",
    version="0.1.0",
    description="Regex script for natural language understanding",
    packages=["nlu_script"],
    python_requires=">=3.5",
    cmdclass={"build_py": BuildPyWithLibrary, "bdist_wheel": BdistWheelAnyPython},
    distclass=BinaryDistribution,
    zip_safe=False,
)
