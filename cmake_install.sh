# build directory name can be a parameter -d
# build type can be a parameter -t
BUILDDIR="build"
BUILDTYPE="Release"
DELBUILD=false
# check number of cores
nproc=$(nproc)
# subtract one
nproc=$((nproc-1))

while getopts d:t:e:j:c:B:v:p: option
do
case "${option}"
in
d) BUILDDIR=${OPTARG};;
t) BUILDTYPE=${OPTARG};;
e) DELBUILD=true;;
j) nproc=${OPTARG};;
c) ACTIVATE_DECODING=false;;
B) BASEDIR=${OPTARG};;
v) VERBOSITY=${OPTARG};;
p) CPLANTBOX_DIR=${OPTARG};;
*) echo "Usage: $0 [-d builddir] [-t buildtype] [-e deletebuild] [-j nproc] [-c activate_decoding] [-B basedir] [-v verbosity] [-p cplantbox_location]"
   echo "  -d builddir       Specify the build directory name (default: build)"
   echo "  -t buildtype     Specify the build type (default: Release)"
   echo "  -e deletebuild    Delete the build directory after building (default: false)"
   echo "  -j nproc          Specify the number of processes to use for building (default: nproc - 1)"
   echo "  -c activate_decoding  Activate decoding (default: false)"
   echo "  -B basedir      Specify the base directory (default: current directory)"
   echo "  -v verbosity      Enable verbose logging (default: false)"
   echo "  -p cplantbox_location  Specify the location of cplantbox (default: not set)"
   exit 1
   ;;
esac
done

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

# Build with apps
SYNAVIS_APPBUILD="-DBUILD_WITH_APPS=On"

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
  echo "No cplantbox location specified, using default."
fi

# configure
cmake -H$DIR -B$DIR/$BUILDDIR -DCMAKE_BUILD_TYPE=$BUILDTYPE -G "$GENERATOR" $LIBDATACHANNEL_VERBOSELOGGING $LIBDATACHANNEL_BUILD_TESTS $LIBDATACHANNEL_BUILD_EXAMPLES $LIBDATACHANNEL_SETTINGS $DECODING -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIRS -DPYTHON_LIBRARY=$PYTHON_LIBRARY $SYNAVIS_APPBUILD $CMAKE_VERBOSE_LOGGING $CPLANTBOX_DIR_OPTION

# build
cmake --build $DIR/$BUILDDIR -- -j $nproc
