#!/bin/bash

rm -rf build
mkdir build
cd build

CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-"Release"}
CMAKE_USE_USHER=${USE_USHER:-"ON"}
CMAKE_NUM_THREADS=${CMAKE_NUM_THREADS:-"8"}
MAKE_NUM_THREADS=${MAKE_NUM_THREADS:-"20"}
LARCH_INCLUDE_TEST=${LARCH_INCLUDE_TEST:-"false"}
LARCH_RUN_TEST=${LARCH_RUN_TEST:-"false"}

echo "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}"
echo "CMAKE_USE_USHER: ${CMAKE_USE_USHER}"
echo "CMAKE_NUM_THREADS: ${CMAKE_NUM_THREADS}"
echo "MAKE_NUM_THREADS: ${MAKE_NUM_THREADS}"
echo "LARCH_INCLUDE_TEST: ${LARCH_INCLUDE_TEST}"
echo "LARCH_RUN_TEST: ${LARCH_RUN_TEST}"
echo "PREFIX: ${PREFIX}"

export CMAKE_NUM_THREADS=${CMAKE_NUM_THREADS}
cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DUSE_USHER=${CMAKE_USE_USHER} -DCMAKE_INSTALL_PREFIX="${PREFIX}" ..
make -j${MAKE_NUM_THREADS}
make install

if [[ ${LARCH_RUN_TEST} == true ]]; then
    echo "RUN TESTS..."
    cd bin
    ln -s ../../data
    ./larch-test -tag slow
    cd ../
fi

# cd install
# cp bin/* $PREFIX/bin
# cp -r lib/larch $PREFIX/lib
