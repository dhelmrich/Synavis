# build directory name can be a parameter -d
# build type can be a parameter -t
# Default values
nproc=$(nproc)
# Default values
BUILDDIR="build"
BUILDTYPE="Release"
DELBUILD=false
ACTIVATE_DECODING=true
PYTHON_ONLY=false
VCPKG_TOOLCHAIN=""
SKIP_COPY_LIBDATACHANNEL=true
# determine parallelism
# subtract one
nproc=$((nproc-1))

# Parse arguments manually
while [[ $# -gt 0 ]]; do
  case "$1" in
    -d) BUILDDIR="$2"; shift 2;;
    -t) BUILDTYPE="$2"; shift 2;;
    -e)
      # Accept -e or -e true/false
      if [[ "$2" == "true" ]]; then
        DELBUILD=true; shift 2
      elif [[ "$2" == "false" ]]; then
        DELBUILD=false; shift 2
      else
        DELBUILD=true; shift
      fi
      ;;
    -j) nproc="$2"; shift 2;;
    -c)
      # Accept -c or -c true/false
      if [[ "$2" == "true" ]]; then
        ACTIVATE_DECODING=true; shift 2
      elif [[ "$2" == "false" ]]; then
        ACTIVATE_DECODING=false; shift 2
      else
        ACTIVATE_DECODING=false; shift
      fi
      ;;
    -B) BASEDIR="$2"; shift 2;;
    -v) VERBOSITY="$2"; shift 2;;
    -p) CPLANTBOX_DIR="$2"; shift 2;;
    -x|--vcpkg) VCPKG_TOOLCHAIN="$2"; shift 2;;
    --python-only)
      PYTHON_ONLY=true; shift;;
    --copy-libdatachannel)
      SKIP_COPY_LIBDATACHANNEL=false; shift;;
    --install-adios2)
      INSTALL_ADIOS2=true; shift;;
    --adios2-root)
      ADIOS2_ROOT="$2"; shift 2;;
    --clang) USE_CLANG=true; shift;;
    -h|--help)
      echo "Usage: $0 [-d builddir] [-t buildtype] [-e deletebuild] [-j nproc] [-c activate_decoding] [-B basedir] [-v verbosity] [-p cplantbox_location] [-x vcpkg_toolchain] [--install-adios2] [--adios2-root] [--clang]"
      echo "  -d builddir       Specify the build directory name (default: build)"
      echo "  -t buildtype      Specify the build type (default: Release)"
      echo "  -e deletebuild    Delete the build directory after building (default: false, accepts optional true/false)"
      echo "  -j nproc          Specify the number of processes to use for building (default: nproc - 1)"
      echo "  -c activate_decoding  Activate decoding (default: false, accepts optional true/false)"
      echo "  -B basedir        Specify the base directory (default: current directory)"
      echo "  -v verbosity      Enable verbose logging (default: false, pass 'true' to enable)"
      echo "  -p cplantbox_location  Specify the location of cplantbox (default: not set)"
      echo "  -x, --vcpkg       Path to vcpkg root directory (toolchain file auto-detected) or to vcpkg.cmake"
      echo "  --clang           Use clang as the C/C++ compiler"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use -h or --help for usage."
      exit 1
      ;;
  esac
done

CMAKEOPT=""

# Set compiler if --clang is used
if [ "$USE_CLANG" = true ]; then
  echo "Using clang as compiler"
  CMAKEOPT="$CMAKEOPT -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
fi

if [ "$DELBUILD" = true ] ; then
  echo "Deleting build directory"
  if [ -d "$BUILDDIR" ]; then
    rm -rf $BUILDDIR
  fi
fi

# store current directory in variable
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ -n "$BASEDIR" ]; then
  DIR=$BASEDIR
fi

# create build directory
# Resolve absolute build directory to avoid duplicating paths when an
# absolute path is provided for -d/BUILDDIR.
case "$BUILDDIR" in
  /*|[A-Za-z]:/*)
    ABS_BUILDDIR="$BUILDDIR"
    ;;
  *)
    ABS_BUILDDIR="$DIR/$BUILDDIR"
    ;;
esac

mkdir -p "$ABS_BUILDDIR"
# CMAKE Options
GENERATOR="Unix Makefiles"
# libdatachannel options
LIBDATACHANNEL_BUILD_TESTS="-DLIBDATACHANNEL_BUILD_TESTS=Off"
LIBDATACHANNEL_BUILD_EXAMPLES="-DLIBDATACHANNEL_BUILD_EXAMPLES=Off"
LIBDATACHANNEL_SETTINGS="-DENABLE_DEBUG_LOGGING=On -DENABLE_LOCALHOST_ADDRESS=On -DENABLE_LOCAL_ADDRESS_TRANSLATION=On"

# default build flags (non-verbose)
BUILD_FLAGS="-j $nproc"
BUILD_FLAGS_QUIET="--quiet"

# basic stuff
PYTHON_INCLUDE_DIRS=$(python3 -c "from distutils.sysconfig import get_python_inc; print(get_python_inc())")
PYTHON_LIBRARY=$(python -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))")
echo "Python include dir: $PYTHON_INCLUDE_DIRS"

# synavis options if DECODING is set
DECODING=""
if [ "$ACTIVATE_DECODING" = true ] ; then
  echo "Activating decoding"
  DECODING="-DBUILD_WITH_DECODING=On"
fi

#cmake verbose option
# CMAKE_VERBOSE="-DCMAKE_VERBOSE_MAKEFILE=On"

# Build with apps (default On). If python-only is requested, turn apps off to speed up build.
SYNAVIS_APPBUILD="-DBUILD_WITH_APPS=On"
if [ "$PYTHON_ONLY" = true ] ; then
  echo "Python-only build requested: disabling app build and limiting build target to PySynavis"
  SYNAVIS_APPBUILD="-DBUILD_WITH_APPS=Off"
fi

# Verbosity
CMAKE_VERBOSE_LOGGING=""
if [ "$VERBOSITY" = true ] ; then
  echo "Enabling verbose logging"
  CMAKE_VERBOSE_LOGGING="-DCMAKE_VERBOSE_MAKEFILE=On"
  BUILD_FLAGS_QUIET=""
fi

CPLANTBOX_DIR_OPTION=""
if [ -n "$CPLANTBOX_DIR" ]; then
  echo "Using cplantbox location: $CPLANTBOX_DIR"
  CPLANTBOX_DIR_OPTION="-DCPlantBox_DIR=$CPLANTBOX_DIR"
else
  echo "No cplantbox location specified, pulling CPlantBox from git"
  # Check if CPlantBox is already cloned
  if [ ! -d "$DIR/CPlantBox" ]; then
    echo "Cloning CPlantBox repository"
    git clone https://github.com/Plant-Root-Soil-Interactions-Modelling/CPlantBox.git
  else
    echo "Found existing CPlantBox directory, using it"
  fi
  # Set the CPlantBox_DIR to the cloned directory
  CPLANTBOX_DIR_OPTION="-DCPlantBox_DIR=$DIR/CPlantBox/build"
fi

# Vcpkg toolchain option (optional)
VCPKG_TOOLCHAIN_OPTION=""
VCPKG_ROOT=""
if [ -n "$VCPKG_TOOLCHAIN" ]; then
  if [ -d "$VCPKG_TOOLCHAIN" ]; then
    VCPKG_ROOT="$VCPKG_TOOLCHAIN"
    VCPKG_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  elif [ -f "$VCPKG_TOOLCHAIN" ]; then
    VCPKG_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    VCPKG_ROOT=$(dirname "$(dirname "$(dirname "$VCPKG_TOOLCHAIN")")")
  fi
  
  if [ -f "$VCPKG_TOOLCHAIN_FILE" ]; then
    echo "Using vcpkg toolchain file: $VCPKG_TOOLCHAIN_FILE"
    echo "Using vcpkg root: $VCPKG_ROOT"
    VCPKG_TOOLCHAIN_OPTION="-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TOOLCHAIN_FILE -DVCPKG_CMAKE_PATH=$VCPKG_TOOLCHAIN_FILE"
  else
    echo "Warning: vcpkg toolchain file not found at: $VCPKG_TOOLCHAIN_FILE"
  fi
fi

# ADIOS2 CMake options
ADIOS2_ROOT_OPTION=""
if [ -n "$ADIOS2_ROOT" ]; then
  echo "Using ADIOS2 root: $ADIOS2_ROOT"
  ADIOS2_ROOT_OPTION="-DADIOS2_ROOT=$ADIOS2_ROOT"
fi

if [ "$INSTALL_ADIOS2" = true ]; then
  SKIP_INSTALL_ADIOS2_OPTION="-DSYNAVIS_SKIP_INSTALL_ADIOS2=Off"
else
  SKIP_INSTALL_ADIOS2_OPTION="-DSYNAVIS_SKIP_INSTALL_ADIOS2=On"
fi

# Install ADIOS2 via vcpkg if requested
if [ "$INSTALL_ADIOS2" = true ] && [ -n "$VCPKG_ROOT" ]; then
  VCPKG_EXE="$VCPKG_ROOT/vcpkg"
  if [ -x "$VCPKG_EXE" ]; then
    echo "Installing adios2[mpi] via vcpkg..."
    "$VCPKG_EXE" install adios2[mpi]:x64-linux --recurse
  else
    echo "Warning: vcpkg executable not found at $VCPKG_EXE; skipping automatic install"
  fi
fi

# configure
cmake -H"$DIR" -B"$ABS_BUILDDIR" -DCMAKE_BUILD_TYPE=$BUILDTYPE -G "$GENERATOR" $LIBDATACHANNEL_BUILD_TESTS $LIBDATACHANNEL_BUILD_EXAMPLES $LIBDATACHANNEL_SETTINGS $DECODING -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIRS -DPYTHON_LIBRARY=$PYTHON_LIBRARY $SYNAVIS_APPBUILD $CMAKE_VERBOSE_LOGGING $CPLANTBOX_DIR_OPTION $VCPKG_TOOLCHAIN_OPTION $CMAKEOPT $SKIP_COPY_OPTION $ADIOS2_ROOT_OPTION $SKIP_INSTALL_ADIOS2_OPTION

# build
if [ "$PYTHON_ONLY" = true ] ; then
  # Build only the Python target to save time
  cmake --build "$ABS_BUILDDIR" --target PySynavis -- $BUILD_FLAGS $BUILD_FLAGS_QUIET
else
  cmake --build "$ABS_BUILDDIR" -- $BUILD_FLAGS $BUILD_FLAGS_QUIET
fi

# Deploy libdatachannel headers and libs into SynavisBackend plugin layout (Unix)
# Only copy libdatachannel artifacts; do NOT copy ffmpeg/libav (clusters usually provide ffmpeg).
# This mirrors the Windows cmake install behavior but keeps ffmpeg handling to the environment.
LIBDATACHANNEL_SRC_INCLUDE="$ABS_BUILDDIR/_deps/libdatachannel-src/include"
LIBDATACHANNEL_BUILD_LIBDIR="$ABS_BUILDDIR/_deps/libdatachannel-build"
DEST_LIBDATA_DIR="$DIR/SynavisBackend/Source/libdatachannel"

# Only perform deploy/copy when explicitly requested via the
# --copy-libdatachannel flag. By default the script skips copying.
if [ "$SKIP_COPY_LIBDATACHANNEL" = false ]; then
  if [ -d "$LIBDATACHANNEL_SRC_INCLUDE" ] || [ -d "$LIBDATACHANNEL_BUILD_LIBDIR" ]; then
    echo "Preparing SynavisBackend libdatachannel layout at: $DEST_LIBDATA_DIR"
    mkdir -p "$DEST_LIBDATA_DIR/include"
    mkdir -p "$DEST_LIBDATA_DIR/lib"

    if [ -d "$LIBDATACHANNEL_SRC_INCLUDE" ]; then
      echo "Copying libdatachannel headers from $LIBDATACHANNEL_SRC_INCLUDE to $DEST_LIBDATA_DIR/include"
      rsync -a --delete "$LIBDATACHANNEL_SRC_INCLUDE/" "$DEST_LIBDATA_DIR/include/"
    else
      echo "libdatachannel source include not found at $LIBDATACHANNEL_SRC_INCLUDE; skipping header copy"
    fi

    # Copy built shared objects or static libs produced by libdatachannel build
    if [ -d "$LIBDATACHANNEL_BUILD_LIBDIR" ]; then
      echo "Copying libdatachannel libraries from $LIBDATACHANNEL_BUILD_LIBDIR to $DEST_LIBDATA_DIR/lib"
      # copy .so and .a artifacts
      find "$LIBDATACHANNEL_BUILD_LIBDIR" -maxdepth 1 -type f \( -name 'libdatachannel.so*' -o -name 'libdatachannel.a' \) -exec cp -v --preserve=mode,timestamps {} "$DEST_LIBDATA_DIR/lib/" \;
      # If SONAMEed file exists with versioned name, ensure unversioned symlink isn't broken
      (cd "$DEST_LIBDATA_DIR/lib" && for f in libdatachannel.so.*; do [ -e "$f" ] && ln -sf "$f" libdatachannel.so || true; done) || true
    else
      echo "libdatachannel build lib dir not found at $LIBDATACHANNEL_BUILD_LIBDIR; skipping library copy"
    fi
  else
    echo "No libdatachannel build outputs found in $ABS_BUILDDIR/_deps; skipping deploy step"
  fi
else
  echo "Skipping libdatachannel deploy (default). Use --copy-libdatachannel to enable."
fi

DEST_ADIOS2_DIR="$DIR/Adios2Backend/Source/adios2"
if [ "$INSTALL_ADIOS2" = true ]; then
  if [ -n "$VCPKG_ROOT" ] && [ -d "$VCPKG_ROOT/installed" ]; then
    echo "Searching vcpkg installed tree for ADIOS2 at: $VCPKG_ROOT/installed"
    for t in "$VCPKG_ROOT"/installed/*; do
      if [ -d "$t/include" ] && { [ -f "$t/include/adios2.h" ] || [ -d "$t/include/adios2" ]; }; then
        echo "Found ADIOS2 in vcpkg triplet: $t"
        echo "Preparing Adios2Backend ADIOS2 layout at: $DEST_ADIOS2_DIR"
        mkdir -p "$DEST_ADIOS2_DIR/include"
        mkdir -p "$DEST_ADIOS2_DIR/lib"
        echo "Copying ADIOS2 headers from $t/include to $DEST_ADIOS2_DIR/include"
        rsync -a --delete "$t/include/" "$DEST_ADIOS2_DIR/include/" 2>/dev/null || cp -rv "$t/include/" "$DEST_ADIOS2_DIR/include/"
        if [ -d "$t/lib" ]; then
          echo "Copying ADIOS2 libs from $t/lib to $DEST_ADIOS2_DIR/lib"
          find "$t/lib" -maxdepth 1 -type f \( -name 'libadios2.so*' -o -name 'libadios2.a' \) -exec cp -v --preserve=mode,timestamps {} "$DEST_ADIOS2_DIR/lib/" \; 2>/dev/null || true
        fi
        break
      fi
    done
  else
    echo "Skipping ADIOS2 deploy: vcpkg not found or ADIOS2 not installed."
  fi
else
  echo "Skipping ADIOS2 deploy. Use --install-adios2 to enable." 
fi
