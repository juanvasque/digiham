add_library(digiham_mbe OBJECT mbe_synthesizer.cpp ambe_modes.cpp)
target_compile_options(digiham_mbe PRIVATE "-fPIC")

add_executable(mbe_synthesizer mbe_synthesizer_cli.cpp cli.cpp)
target_link_libraries(mbe_synthesizer digiham)
install(TARGETS mbe_synthesizer DESTINATION ${CMAKE_INSTALL_BINDIR})