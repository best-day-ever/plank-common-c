find_package(Threads REQUIRED)

function(add_plank_input_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE moonlight-common-c Threads::Threads)
  target_include_directories(${name} PRIVATE "${PLANK_TRANSPORT_DIR}/include")
  if (NOT WIN32)
    target_link_libraries(${name} PRIVATE m)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 20)
endfunction()

add_plank_input_test(plank-native-input-wire native-input-wire.c)
add_plank_input_test(plank-queue-ownership queue-ownership.c)

# GNU/LLVM ELF link wrapping supplies deterministic scheduler boundaries with
# no production hooks. Static linking is required to wrap worker calls.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  get_target_property(common_library_type moonlight-common-c TYPE)
  if (common_library_type STREQUAL "STATIC_LIBRARY")
    add_plank_input_test(plank-native-input-replacement native-input-replacement.c)
    target_compile_definitions(plank-native-input-replacement PRIVATE PLANK_TEST_MATCHED_POLL)
    set_property(TARGET plank-native-input-replacement APPEND_STRING PROPERTY LINK_FLAGS
      " -Wl,--wrap=LbqPeekQueueElement -Wl,--wrap=LbqPollQueueElementIf")
  endif()
endif()
