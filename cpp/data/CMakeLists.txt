set(FRAMEPROCESSOR_DIR ${DATA_DIR}/frameProcessor)
set(FRAMESIMULATOR_DIR ${DATA_DIR}/frameSimulator)
set(TEST_DIR ${DATA_DIR}/test)

include_directories(${DATA_DIR}/include)
include_directories(${DATA_DIR}/common/include)

add_subdirectory(${FRAMEPROCESSOR_DIR})
add_subdirectory(${TEST_DIR})
