#!/bin/bash
set -e

remove_conda_paths() {
    local var_name="$1"
    local value="${!var_name}"
    local out=""
    local entry

    IFS=':' read -ra entries <<< "$value"
    for entry in "${entries[@]}"; do
        if [[ -z "$entry" ]]; then
            continue
        fi
        if [[ "$entry" == *"/miniforge"* || "$entry" == *"/conda"* || "$entry" == *"/anaconda"* ]]; then
            continue
        fi
        if [[ -z "$out" ]]; then
            out="$entry"
        else
            out="$out:$entry"
        fi
    done

    export "$var_name=$out"
}

# deactivate conda for this build
if [ -n "$CONDA_PREFIX" ]; then
    echo "Deactivating conda environment: $CONDA_PREFIX"
    if command -v conda >/dev/null 2>&1; then
        conda deactivate || echo "Conda deactivate failed in non-interactive shell; sanitizing environment manually"
    fi
else
    echo "No conda environment detected, proceeding with build"
fi

unset CONDA_PREFIX CONDA_DEFAULT_ENV CONDA_EXE _CE_CONDA _CE_M
remove_conda_paths PATH
remove_conda_paths LD_LIBRARY_PATH
remove_conda_paths LIBRARY_PATH
remove_conda_paths CPATH
remove_conda_paths CMAKE_PREFIX_PATH
remove_conda_paths PKG_CONFIG_PATH

echo "=== Building ADIOS2 without conda environment ==="

BUILD_DIR="/home/baker/work/PlantVis/ADIOS2/build_unix"
SOURCE_DIR="/home/baker/work/PlantVis/ADIOS2"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Force pkg-config and loader to prefer system locations.
export PKG_CONFIG_PATH="/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"

echo "=== Toolchain sanity ==="
echo "PATH=$PATH"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
echo "pkg-config libcurl: $(pkg-config --modversion libcurl)"
echo "pkg-config flags: $(pkg-config --cflags --libs libcurl)"
echo "mpicc: $(command -v mpicc || true)"

# Reset stale cache so old conda-discovered libs cannot linger.
rm -f CMakeCache.txt
rm -rf CMakeFiles

cmake "$SOURCE_DIR" \
    -DCMAKE_INSTALL_PREFIX=/home/baker/work/PlantVis/ADIOS2/install \
    -DADIOS2_USE_SST=ON \
    -DADIOS2_USE_HDF5=ON \
    -DADIOS2_BUILD_SHARED_LIBS=ON \
    -DADIOS2_BUILD_C_BINDINGS=ON \
    -DADIOS2_BUILD_EXAMPLES=OFF \
    -DADIOS2_BUILD_TESTING=OFF \
    -DADIOS2_USE_CURL=ON \
    -DCURL_INCLUDE_DIR=/usr/include/x86_64-linux-gnu \
    -DCURL_LIBRARY=/usr/lib/x86_64-linux-gnu/libcurl.so \
    -DMPI_C_COMPILER=/usr/bin/mpicc \
    -DMPI_CXX_COMPILER=/usr/bin/mpicxx

echo "=== Building ADIOS2 ==="
cmake --build . -j8

echo "=== Installing ADIOS2 ==="
cmake --build . --target install

echo "=== ADIOS2 build complete ==="
echo "Install location: /home/baker/work/PlantVis/ADIOS2/install"
