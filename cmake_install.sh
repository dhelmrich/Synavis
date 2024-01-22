# build directory name can be a parameter -d
# build type can be a parameter -t
BUILDDIR="build"
BUILDTYPE="Release"
DELBUILD=false
while getopts d:t:e: option
do
case "${option}"
in
d) BUILDDIR=${OPTARG};;
t) BUILDTYPE=${OPTARG};;
e) DELBUILD=true;;
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
# abort if build directory exists
if [ -d "$DIR/$BUILDDIR" ]; then
  echo "Build directory exists. Aborting."
  exit 1
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

# synavis options
DECODING="-DBUILD_WITH_DECODING=On"

#cmake verbose option
CMAKE_VERBOSE="-DCMAKE_VERBOSE_MAKEFILE=On"

# configure
cmake -H$DIR -B$DIR/$BUILDDIR -DCMAKE_BUILD_TYPE=$BUILDTYPE -G "$GENERATOR" $LIBDATACHANNEL_VERBOSELOGGING $LIBDATACHANNEL_BUILD_TESTS $LIBDATACHANNEL_BUILD_EXAMPLES $LIBDATACHANNEL_SETTINGS $DECODING -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIRS -DPYTHON_LIBRARY=$PYTHON_LIBRARY

# build
# check number of cores
nproc=$(nproc)
# subtract one
nproc=$((nproc-1))
# build
cmake --build $DIR/$BUILDDIR --target install -- -j $nproc
