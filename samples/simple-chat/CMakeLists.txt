add_executable(simple-chat
				${CMAKE_CURRENT_SOURCE_DIR}/simple-chat.cpp
				${CMAKE_CURRENT_SOURCE_DIR}/protocol.hpp
				${CMAKE_CURRENT_SOURCE_DIR}/protocol_impl.cpp)
target_link_libraries(simple-chat rak-service RakNetLibStatic)