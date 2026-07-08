add_executable(refcomp_tests
    main.cpp
    cases/test_null_modes.cpp
    cases/test_opt_vs_scalar.cpp
    cases/test_adaa.cpp
    cases/test_freq_response.cpp
    cases/test_aliasing.cpp
    cases/test_step_response.cpp
    cases/test_latency.cpp
    cases/test_cpu_bench.cpp
    cases/test_flaws.cpp
    cases/test_voice.cpp
    cases/test_optimal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kissfft/kiss_fft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kissfft/kiss_fftr.c)

target_include_directories(refcomp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kissfft
    ${CMAKE_CURRENT_SOURCE_DIR}/harness)

# Analysis FFT in double precision.
target_compile_definitions(refcomp_tests PRIVATE kiss_fft_scalar=double)
target_compile_options(refcomp_tests PRIVATE -O3)
