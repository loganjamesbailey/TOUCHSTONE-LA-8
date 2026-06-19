# Offline render CLI. JUCE-free: links nothing, just the header-only DSP
# core. Builds the same Engine<float, FastMath> the plugin ships.
add_executable(touchstone_render render.cpp)

target_include_directories(touchstone_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(touchstone_render PRIVATE -O3)
