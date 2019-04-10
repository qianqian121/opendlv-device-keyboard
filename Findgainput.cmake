find_path(GAINPUT_INCLUDE_DIR /usr/local/include/gainput/gainput.h)

find_library(GAINPUT_LIBRARY gainput PATHS /usr/local/lib)

set(GAINPUT_LIBRARIES ${GAINPUT_LIBRARY} )
set(GAINPUT_INCLUDE_DIRS ${GAINPUT_INCLUDE_DIR} )
