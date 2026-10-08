# Build against the Windows SDK audio headers, independently of DirectX 9 includes.
add_library(MatrixAudio STATIC Audio.cpp XAudio2.cpp Audio.hpp)
target_include_directories(MatrixAudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET MatrixAudio PROPERTY INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(MatrixAudio PUBLIC cxx_std_20)
target_compile_options(MatrixAudio PRIVATE ${COMPILE_OPTIONS} "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
target_compile_definitions(MatrixAudio PRIVATE ${COMPILE_DEFINITIONS} NOMINMAX _ALLOW_RTCc_IN_STL)
target_link_libraries(MatrixAudio PRIVATE ole32)
