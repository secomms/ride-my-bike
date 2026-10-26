# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0"


if(USE_NIST_RAND)
  FILE(GLOB HEADERS ${CMAKE_CURRENT_LIST_DIR}/*.h)
  target_sources(bike-test
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/FromNIST/rng.c 
      ${CMAKE_CURRENT_LIST_DIR}/FromNIST/PQCgenKAT_kem.c
      
      ${HEADERS}
  )
else()
  target_sources(bike-test
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/main_test.c
  )
endif()

option(BUILD_DFR_TEST "Also build the standalone BIKE DFR test (test_dfr.c) as bike-dfr-test" OFF)

if(BUILD_DFR_TEST)
  add_executable(bike-dfr-test ${CMAKE_CURRENT_LIST_DIR}/test_dfr.c)
  target_link_libraries(bike-dfr-test PRIVATE bike m)
endif()

option(BUILD_CC_TEST "Also build the standalone BIKE CC test (test_cc.c) as cc-test" OFF)

if(BUILD_CC_TEST)
  add_executable(cc-test ${CMAKE_CURRENT_LIST_DIR}/test_cc.c)
  target_link_libraries(cc-test PRIVATE bike m cpucycles)
endif()

option(BUILD_CC_BINNED_TEST "--------" OFF)

if(BUILD_CC_BINNED_TEST)
  add_executable(cc-binned-test ${CMAKE_CURRENT_LIST_DIR}/test_cc_binned.c)
  target_link_libraries(cc-binned-test PRIVATE bike m cpucycles)
endif()
