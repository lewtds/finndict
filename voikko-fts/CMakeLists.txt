project(voikko_fts)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)

include_directories(BEFORE /home/lewtds/dev/finndict/libvoikko-4.1.1/src)
link_directories(/home/lewtds/dev/finndict/libvoikko-4.1.1/src/.libs)

add_library(${PROJECT_NAME} SHARED ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} voikko)

add_executable(noice ${SRC_LIST})
target_link_libraries(noice voikko)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -DDEBUG")

enable_testing()
add_test(
	NAME smoketest
	COMMAND sqlite3 ../voikko-fts/dict.db < ../voikko-fts/test.sql)
