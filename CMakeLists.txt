cmake_minimum_required(VERSION 3.6.3)
project(webcamvb)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCE_FILES src/**.cc src/**.cpp src/**.h src/**.hpp build/**.cc build/**.cpp build/**.h build/**.hpp)

set(dir_list "")
foreach(sourcefile ${SOURCE_FILES})
    get_filename_component(dir ${sourcefile} PATH)
    while(NOT "${dir}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
        if(IS_DIRECTORY ${dir})
            if (NOT dir IN_LIST dir_list)
                # message(STATUS ${dir})
                set(dir_list ${dir_list} ${dir})
            endif()
        endif()
        get_filename_component(dir ${dir} PATH)
    endwhile()
endforeach()

list(REMOVE_DUPLICATES dir_list)

foreach(dir ${dir_list})
    message(STATUS ${dir})
    include_directories(${dir})
endforeach()

add_executable(webcamvb ${SOURCE_FILES})
