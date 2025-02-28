import os
import re
import sys
import platform
import subprocess
import distutils

from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

        if platform.system() == "Windows":
            cmake_version = LooseVersion(re.search(r'version\s*([\d.]+)', out.decode()).group(1))
            if cmake_version < '3.1.0':
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
                      '-DPYTHON_EXECUTABLE=' + sys.executable]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        parallel = self.parallel if self.parallel else 2
        parallel_opt = '-j{}'.format(parallel)

        if platform.system() == "Windows":
            cmake_args += ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), extdir)]
            if sys.maxsize > 2**32:
                cmake_args += ['-A', 'x64']
            build_args += ['--', '/m']
        else:
            cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
            build_args += ['--', parallel_opt]

        env = os.environ.copy()
        if 'NCCL_SUPPORTS_BFLOAT16' in env:
            cmake_args += ['-DNCCL_SUPPORTS_BFLOAT16=' + env['NCCL_SUPPORTS_BFLOAT16']]

        if 'Torch_DIR' not in env:
            env['Torch_DIR'] = os.path.join(distutils.sysconfig.get_python_lib(), 'torch')

        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(env.get('CXXFLAGS', ''),
                                                              self.distribution.get_version())
        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)

setup(
    name='pyrannc',
    packages=find_packages(),
    version='0.6.0rc1',
    author='Masahiro Tanaka',
    author_email='mtnk@nict.go.jp',
    description='Deep learning framework for data/model hybrid parallelism',
    long_description='',
    ext_modules=[CMakeExtension('pyrannc._pyrannc')],
    cmdclass=dict(build_ext=CMakeBuild),
    zip_safe=False,
)
