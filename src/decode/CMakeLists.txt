# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0"

FILE(GLOB HEADERS ${CMAKE_CURRENT_LIST_DIR}/*.h)
target_sources(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/decode.c
    ${HEADERS}
)


##########################################################################################
# DECODER SOURCE FILE SELECTION 
##########################################################################################
# BFmax decoder
if(DECODER STREQUAL "BFMAX")
  target_sources(${PROJECT_NAME}
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/decode_bfmax_avx2.c
  )
  
  if(USE_AVX512)
    target_sources(${PROJECT_NAME}
      PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/decode_bfmax_avx512.c
    )
  # For the source file of AVX512
  endif()

# Hybrid decoder
elseif(DECODER STREQUAL "HYBRID")
  target_sources(${PROJECT_NAME}
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/decode_hybrid_avx2.c
      ${CMAKE_CURRENT_LIST_DIR}/decode_portable.c
      ${CMAKE_CURRENT_LIST_DIR}/decode_avx2.c
  )

# OOP decoder
elseif(DECODER STREQUAL "OOP")
  target_sources(${PROJECT_NAME}
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/decode_oop_avx2.c
      ${CMAKE_CURRENT_LIST_DIR}/decode_portable.c
  )
  if(X86_64)
    target_sources(${PROJECT_NAME}
      PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/decode_avx2.c
    )
    if(USE_AVX512)
      target_sources(${PROJECT_NAME}
        PRIVATE
          ${CMAKE_CURRENT_LIST_DIR}/decode_avx512.c
      )
    endif()
  endif()

# EXOTIC decoder
elseif(DECODER STREQUAL "EXOTIC")
  target_sources(${PROJECT_NAME}
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/decode_exotic_avx2.c
      ${CMAKE_CURRENT_LIST_DIR}/decode_portable.c
  )
  if(X86_64)
    target_sources(${PROJECT_NAME}
      PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/decode_avx2.c
    )
    if(USE_AVX512)
      target_sources(${PROJECT_NAME}
        PRIVATE
          ${CMAKE_CURRENT_LIST_DIR}/decode_avx512.c
      )
    endif()
  endif()

# BGF Decoder
elseif(DECODER STREQUAL "BGF")
  target_sources(${PROJECT_NAME}
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/decode_portable.c
  )
  if(X86_64)
    target_sources(${PROJECT_NAME}
      PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/decode_avx2.c
    )
    if(USE_AVX512)
      target_sources(${PROJECT_NAME}
        PRIVATE
          ${CMAKE_CURRENT_LIST_DIR}/decode_avx512.c
      )
    endif()
  endif()
endif()