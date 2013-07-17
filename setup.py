import sys
import os
import os.path
import fnmatch
import platform

try:
  from setuptools import Extension, setup
except ImportError:
  from distutils.core import Extension, setup

def is_support_version():
    return sys.hexversion >= 0x03020000

def is_py32():
    return sys.hexversion >= 0x03020000

def check_requirements():
    if not is_support_version():
        print("support only Python3.2+")
        sys.exit(1)

    if "posix" not in os.name:
        print("Are you really running a posix compliant OS ?")
        print("Be posix compliant is mandatory")
        sys.exit(1)

check_requirements()


if os.environ.get("JEGA_DEVELOP") == "1":
    dev = ("DEVELOP", None)
else:
    dev = ("PRODUCT", None)

library_dirs=['/usr/local/lib']

##### utility functions
def read(name):
    f = open(os.path.join(os.path.dirname(__file__), name))
    text = f.read()
    f.close()
    return text

def get_sources(path, ignore_files):
    src = []
    for root, dirs , files in os.walk(path):
        for file in files:
            src_path = os.path.join(root, file)
            #ignore = reduce(lambda x, y: x or y, [fnmatch.fnmatch(src_path, i) for i in ignore_files])
            ignore = [i for i in ignore_files if  fnmatch.fnmatch(src_path, i)]
            if not ignore and src_path.endswith(".c"):
                src.append(src_path)
    return src


include_dirs = ["lib/greenlet", "lib/greenlet/platform", "lib/picoev", "lib/c-ares"]

sources = get_sources("src", ["picoev_*"])

libraries=[]
if "Linux" == platform.system():
    picoev = 'lib/picoev/picoev_epoll.c'
    libraries=["rt"]
elif "Darwin" == platform.system():
    picoev = 'lib/picoev/picoev_kqueue.c'
elif "FreeBSD" == platform.system():
    picoev = 'lib/picoev/picoev_kqueue.c'
else:
    print("Sorry, Linux or BSD or MacOS only.")
    sys.exit(1)

sources.append(picoev)
c_ares_src = get_sources("lib/c-ares", [""])
sources.extend(c_ares_src)
greenlet_src = get_sources("lib/greenlet", [""])
sources.extend(greenlet_src)

ares_configure_command = [os.path.abspath('lib/c-ares/configure'), 'CONFIG_COMMANDS=', 'CONFIG_FILES=']

# c-ares
def need_configure():
    if not os.path.exists('lib/c-ares/ares_config.h'):
        return True

def configure_c_ares():
    if need_configure():
        os.chmod(ares_configure_command[0], 493)
        rc = os.system('cd lib/c-ares && %s' % ' '.join(ares_configure_command))

##### extensions
core = Extension(name="_jega",
                 sources=sources,
                 libraries=libraries,
                 #libraries=["rt", "profiler"],
                 #library_dirs=library_dirs,
                 include_dirs=include_dirs,
                 define_macros=[
                     dev,
                     ('HAVE_CONFIG_H', None),
                 ],
                 #configure = configure_c_ares,
                 )
ext_modules = [core]

##### properties
version = "0.1"
name = "jega"
short_description = "Jega is a concurrent networking and cooperative multitasking library for Python3."
classifiers = [
    'Programming Language :: C',
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.2",
    "Programming Language :: Python :: 3.3",
    'Operating System :: POSIX :: Linux',
    'License :: OSI Approved :: BSD License',
    "Topic :: Internet",
    "Topic :: Software Development :: Libraries :: Python Modules",
    "Intended Audience :: Developers",
    "Development Status :: 3 - Alpha",
  ]

extra = {}

##### setp

configure_c_ares()

setup(name = name,
      version = version,
      description = short_description,
      long_description = read("README.rst"),
      packages = ['jega', 'jega.futures', 'jega.ext'],
      author='yutaka matsubara',
      author_email='yutaka.matsubara@gmail.com',
      url = "https://github.com/mopemope/jega",
      license='BSD',
      ext_modules = ext_modules,
      classifiers = classifiers,
      platforms='Linux, BSD',
      install_requires=[
       'greenlet>=0.4.0,<0.5'
      ],
      **extra
)

