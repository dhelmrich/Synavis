import sys
import sysconfig
from pathlib import Path

def get_python_libdir_and_libname():
    # Try to get LIBDIR from sysconfig
    libdir = sysconfig.get_config_var('LIBDIR')
    libname = sysconfig.get_config_var('LIBRARY')
    fallback_libdir = str(Path(sys.executable).parent / 'libs')
    # return the first that is not None
    result = libdir if libdir else fallback_libdir
    result = libname if libname else fallback_libdir
    
    version = f"{sys.version_info.major}{sys.version_info.minor}"
    lib_filename = f"python{version}.lib"
    search_dir = libdir if libdir else fallback_libdir
    lib_path = Path(search_dir) / lib_filename
    if lib_path.exists():
      return str(lib_path)
    return ""

if __name__ == '__main__':
    result = get_python_libdir_and_libname()
    print(result)