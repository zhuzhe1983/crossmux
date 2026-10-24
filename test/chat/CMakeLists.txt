add_executable(ChatProtocolTest ChatProtocolTest.cpp)
target_include_directories(ChatProtocolTest PRIVATE ../../src)
target_link_libraries(ChatProtocolTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ChatProtocolTest)

add_executable(ChatStoreTest ChatStoreTest.cpp ../../src/activities/apps/chat/ChatStore.cpp)
target_include_directories(ChatStoreTest PRIVATE stubs ../../src)
target_link_libraries(ChatStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ChatStoreTest)

# Compile the production backend with only its Session boundary replaced.
# No transport/ACK logic is duplicated by the test harness.
set(esp_backend_source "${CMAKE_CURRENT_SOURCE_DIR}/../../src/activities/apps/chat/EspNowBackend.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${esp_backend_source}")
file(READ "${esp_backend_source}" esp_backend)
string(REPLACE "\"ChatSession.h\"" "<EspTestSession.h>" esp_backend "${esp_backend}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/EspNowBackendHost.cpp" "${esp_backend}")
add_executable(EspChatTest EspChatTest.cpp "${CMAKE_CURRENT_BINARY_DIR}/EspNowBackendHost.cpp")
target_include_directories(EspChatTest PRIVATE esp_stubs stubs ../../src ../../src/activities/apps/chat)
target_compile_definitions(EspChatTest PRIVATE CROSSMUX_ESP_CHAT=1)
target_link_libraries(EspChatTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EspChatTest)

add_test(NAME ToxLinkage
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_tox_linkage.py)
set_tests_properties(ToxLinkage PROPERTIES SKIP_REGULAR_EXPRESSION "skipped=")

add_test(NAME WifiIdentityQr
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_wifi_identity_qr.py)
set_tests_properties(WifiIdentityQr PROPERTIES SKIP_REGULAR_EXPRESSION "skipped=")
