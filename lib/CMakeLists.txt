include_directories(../include)
include_directories(${BARE_METAL_OS_SOURCE_DIR}/programs)

set(bare_c++lib_SOURCES
    csetjmp.s
    cstdlib.cpp
    facets.cpp
    istream.cpp
    iostream.cpp
    new.cpp
    num_put.cpp
    ostream.cpp
    start/atexit.cpp
    start/pure_virtual.cpp
    stream_iterator.cpp
    unwind/unwind.cpp
    video.cpp
)

add_library(bare_c++lib ${bare_c++lib_SOURCES})
