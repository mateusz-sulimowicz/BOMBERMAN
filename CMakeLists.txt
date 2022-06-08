project(robots-client)
cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_COMPILER "/opt/gcc-11.2/bin/g++-11.2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wconversion -Werror -O2 -std=gnu++20")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath -Wl,/opt/gcc-11.2/lib64")

# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
    _add_executable(${ARGV})
    if (TARGET ${_name})
        target_link_libraries(${_name} ${Boost_LIBRARIES} pthread)
    endif()
endmacro()

find_package( Boost REQUIRED program_options)
include_directories( ${Boost_INCLUDE_DIR} )

add_executable(robots-client ./client/robots-client.cpp ./client/tcp-connection.h ./client/types.h ./client/events.h
	./client/server.h ./client/udp-socket.h ./client/gui.h)
 
add_executable(robots-server ./server/robots-server.cpp ./server/tcp-connection.h ./server/types.h ./server/events.h
	./server/client-handler.h ./server/client-acceptor.h ./server/server.h ./server/blocking-queue.h ./server/messages.h
	./server/game-manager.h)

install(TARGETS DESTINATION .)
