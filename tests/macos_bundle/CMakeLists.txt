# No ROM, generated guest code, RT64, SDL or font assets. This compiles the real
# Cocoa dialog backend and exercises relocation of a real linked dylib only.
add_library(srw64-bundle-fixture SHARED fixture.cpp)
set_target_properties(srw64-bundle-fixture PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/macos-smoke")
add_executable(srw64-macos-smoke main.cpp)
target_link_libraries(srw64-macos-smoke PRIVATE srw64_desktop_macos srw64-bundle-fixture)
set_target_properties(srw64-macos-smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/macos-smoke")
add_test(NAME native-desktop-macos COMMAND srw64-macos-smoke --help)
