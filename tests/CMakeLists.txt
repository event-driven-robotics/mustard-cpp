cmake_minimum_required(VERSION 3.21)

# Helper: add a test executable linked against gtest + mustard core
function(mustard_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE mustard GTest::gtest_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---------------------------------------------------------------------------
# Unit tests (no file I/O, no OpenGL)
# ---------------------------------------------------------------------------
mustard_add_test(test_chunk_cache      test_chunk_cache.cpp)
mustard_add_test(test_callback_pipeline test_callback_pipeline.cpp)

# Synthetic-data loader test (writes a temp file in /tmp — no sample files)
mustard_add_test(test_iitdatalog_loader test_iitdatalog_loader.cpp)

# ---------------------------------------------------------------------------
# Integration test — uses the real data.log sample (git-lfs tracked)
# ---------------------------------------------------------------------------
set(IITDATALOG_SAMPLE_PATH "${CMAKE_SOURCE_DIR}/data_samples/iitdatalog/data.log")

add_executable(test_iitdatalog_integration test_iitdatalog_integration.cpp)
target_link_libraries(test_iitdatalog_integration PRIVATE mustard GTest::gtest_main)
target_include_directories(test_iitdatalog_integration PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(test_iitdatalog_integration PRIVATE
    IITDATALOG_SAMPLE_PATH="${IITDATALOG_SAMPLE_PATH}"
)
add_test(NAME test_iitdatalog_integration COMMAND test_iitdatalog_integration)
