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
    --clang) USE_CLANG=true; shift;;
    -h|--help)
      echo "Usage: $0 [-d builddir] [-t buildtype] [-e deletebuild] [-j nproc] [-c activate_decoding] [-B basedir] [-v verbosity] [-p cplantbox_location] [-x vcpkg_toolchain] [--clang]"
      echo "  -d builddir       Specify the build directory name (default: build)"
      echo "  -t buildtype      Specify the build type (default: Release)"
      echo "  -e deletebuild    Delete the build directory after building (default: false, accepts optional true/false)"
      echo "  -j nproc          Specify the number of processes to use for building (default: nproc - 1)"
      echo "  -c activate_decoding  Activate decoding (default: false, accepts optional true/false)"
      echo "  -B basedir        Specify the base directory (default: current directory)"
      echo "  -v verbosity      Enable verbose logging (default: false)"
      echo "  -p cplantbox_location  Specify the location of cplantbox (default: not set)"
      echo "  -x, --vcpkg       Path to vcpkg's buildsystems/vcpkg.cmake (project expects this in VCPKG_CMAKE_PATH)"
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
mkdir -p $DIR/$BUILDDIR
# CMAKE Options
GENERATOR="Unix Makefiles"
# libdatachannel options
LIBDATACHANNEL_BUILD_TESTS="-DLIBDATACHANNEL_BUILD_TESTS=Off"
LIBDATACHANNEL_BUILD_EXAMPLES="-DLIBDATACHANNEL_BUILD_EXAMPLES=Off"
LIBDATACHANNEL_SETTINGS="-DENABLE_DEBUG_LOGGING=On -DENABLE_LOCALHOST_ADDRESS=On -DENABLE_LOCAL_ADDRESS_TRANSLATION=On"

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
CMAKE_VERBOSE="-DCMAKE_VERBOSE_MAKEFILE=On"

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
if [ -n "$VCPKG_TOOLCHAIN" ]; then
  echo "Using vcpkg toolchain file: $VCPKG_TOOLCHAIN"
  VCPKG_TOOLCHAIN_OPTION="-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TOOLCHAIN -DVCPKG_CMAKE_PATH=$VCPKG_TOOLCHAIN"
fi

# configure
cmake -H$DIR -B$DIR/$BUILDDIR -DCMAKE_BUILD_TYPE=$BUILDTYPE -G "$GENERATOR" $LIBDATACHANNEL_VERBOSELOGGING $LIBDATACHANNEL_BUILD_TESTS $LIBDATACHANNEL_BUILD_EXAMPLES $LIBDATACHANNEL_SETTINGS $DECODING -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIRS -DPYTHON_LIBRARY=$PYTHON_LIBRARY $SYNAVIS_APPBUILD $CMAKE_VERBOSE_LOGGING $CPLANTBOX_DIR_OPTION $VCPKG_TOOLCHAIN_OPTION $CMAKEOPT

# build
if [ "$PYTHON_ONLY" = true ] ; then
  # Build only the Python target to save time
  cmake --build $DIR/$BUILDDIR --target PySynavis -- -j $nproc
else
  cmake --build $DIR/$BUILDDIR -- -j $nproc
fi

# Deploy libdatachannel headers and libs into SynavisBackend plugin layout (Unix)
# Only copy libdatachannel artifacts; do NOT copy ffmpeg/libav (clusters usually provide ffmpeg).
# This mirrors the Windows cmake install behavior but keeps ffmpeg handling to the environment.
LIBDATACHANNEL_SRC_INCLUDE="$DIR/$BUILDDIR/_deps/libdatachannel-src/include"
LIBDATACHANNEL_BUILD_LIBDIR="$DIR/$BUILDDIR/_deps/libdatachannel-build"
DEST_LIBDATA_DIR="$DIR/SynavisBackend/Source/libdatachannel"

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
  echo "No libdatachannel build outputs found in $DIR/$BUILDDIR/_deps; skipping deploy step"
fi
