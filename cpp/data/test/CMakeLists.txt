# add_subdirectory(integrationTest)

# set(CMAKE_INCLUDE_CURRENT_DIR on)
# ADD_DEFINITIONS(-DBOOST_TEST_DYN_LINK)

# include_directories(${Boost_INCLUDE_DIRS} ${LOG4CXX_INCLUDE_DIRS}/.. ${ZEROMQ_INCLUDE_DIRS})

# # Build list of test source files from current dir
# file(GLOB TEST_SOURCES *.cpp)

# # Build list of main project source files from src dir but exclude application main
# list(REMOVE_ITEM APP_SOURCES ${APP_MAIN_SOURCE})

# if ( ${CMAKE_SYSTEM_NAME} MATCHES Linux )
# # librt required for timing functions
# find_library(REALTIME_LIBRARY 
# 		NAMES rt)
# endif()

