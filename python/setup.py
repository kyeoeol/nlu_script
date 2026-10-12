"""Builds the nlu_script shared library with CMake and packages it with the
ctypes binding: `pip install ./python`."""
import glob
import os
import shutil
import subprocess

from setuptools import setup
from setuptools.command.build_py import build_py
from setuptools.dist import Distribution

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class BuildPyWithLibrary(build_py):
    def run(self):
        build_py.run(self)
        build_dir = os.path.join(HERE, "build", "cmake")
        subprocess.check_call(["cmake", "-S", ROOT, "-B", build_dir,
                               "-DCMAKE_BUILD_TYPE=Release", "-DNLU_SCRIPT_BUILD_TESTS=OFF"])
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


setup(
    name="nlu_script",
    version="0.1.0",
    description="Regex script for natural language understanding",
    packages=["nlu_script"],
    python_requires=">=3.5",
    cmdclass={"build_py": BuildPyWithLibrary},
    distclass=BinaryDistribution,
    zip_safe=False,
)
