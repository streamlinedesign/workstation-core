add_executable(wsc-tests
  test_main.cpp
  test_ring_buffer.cpp
  test_midi_parser.cpp
  test_sine_engine.cpp
)
target_link_libraries(wsc-tests PRIVATE wsc)
target_compile_options(wsc-tests PRIVATE -Wall -Wextra)
add_test(NAME wsc-tests COMMAND wsc-tests)
