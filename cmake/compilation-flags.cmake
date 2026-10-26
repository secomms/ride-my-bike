# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  set(CLANG 1)
else()
  set(CLANG 0)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb -fPIC -std=c99")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunused -Wcomment -Wchar-subscripts -Wuninitialized -Wshadow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wwrite-strings -Wformat-security -Wcast-qual -Wunused-result")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funroll-loops")

# gcc bug says bfmax_ctx_init is unused?
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function")
# error: AVX vector return without AVX enabled changes the ABI
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")

if(X86_64)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -mno-red-zone")
elseif(X86)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -mno-red-zone")
endif()

# Avoiding GCC 4.8 bug
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-braces -Wno-missing-field-initializers")

if(NOT DEFINED VERBOSE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-align")
endif()

if(MSAN)
  if(NOT CLANG)
    message(FATAL_ERROR "Cannot enable MSAN unless using Clang")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer")
endif()

if(ASAN)
  if(NOT CLANG)
    message(FATAL_ERROR "Cannot enable ASAN unless using Clang")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer")
endif()

if(TSAN)
  if(NOT CLANG)
    message(FATAL_ERROR "Cannot enable TSAN unless using Clang")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
endif()

if(UBSAN)
  if(NOT CLANG)
    message(FATAL_ERROR "Cannot enable UBSAN unless using Clang")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
endif()

if(RDTSC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRDTSC")
endif()

if(VERBOSE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVERBOSE=${VERBOSE}")
endif()

if(FIXED_SEED)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DFIXED_SEED")
endif()

if(NUM_OF_TESTS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNUM_OF_TESTS=${NUM_OF_TESTS}")
endif()

if(LEVEL)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLEVEL=${LEVEL}")
endif()

option(RELIABILITY_LEVEL "Reliability level" 5)
if(RELIABILITY_LEVEL)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRELIABILITY_LEVEL=${RELIABILITY_LEVEL}")
endif()

option(UNIFORM_SAMPLING "Uniform sampling" ON)
if(UNIFORM_SAMPLING)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUNIFORM_SAMPLING=1")
endif()

if(BIND_PK_AND_M)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBIND_PK_AND_M=1")
endif()

# SHA2 is the default in Lean-BIKE
option(USE_AES_AND_SHA2 "Use AES and SHA2" ON)
if(NOT USE_AES_AND_SHA2)
  set(USE_SHA3_AND_SHAKE ON)
endif()

# Using SHA3 and SHAKE forces the standalone implementation
if(USE_SHA3_AND_SHAKE)
  set(STANDALONE_IMPL ON)
endif()

# Standalone implementation features an implementation of AES that uses
# AES-NI and SSE3 x86 instructions. This means that the implementation
# that uses AES based PRF is not fully portable. However, if SHAKE based
# PRF is used (USE_SHA3_AND_SHAKE flag is set) then the implementation
# is fully portable because SHA3 and SHAKE are implemented in pure C.
if(STANDALONE_IMPL)
  if((NOT X86_64) AND (NOT X86) AND (NOT USE_SHA3_AND_SHAKE))
    message(FATAL_ERROR " Standalone implementation with AES based PRNG works only on x86 systems.")
  endif()

  if(USE_SHA3_AND_SHAKE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_SHA3_AND_SHAKE=1")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -maes -mssse3")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSTANDALONE_IMPL=1")
else()
  set(LINK_OPENSSL 1)
endif()

if(USE_NIST_RAND)
  if(FIXED_SEED)
    message(FATAL "Can't set both FIXED_SEED and USE_NIST_RAND")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_NIST_RAND")

  # This code depends on OpenSSL
  set(LINK_OPENSSL 1)

endif()


##########################################################################################
# AVX2 OPTIMIZATION 
##########################################################################################
FILE(GLOB_RECURSE AVX2_SRCS ${PROJECT_SOURCE_DIR}/src/*_avx2.c)
set_source_files_properties(${AVX2_SRCS} PROPERTIES COMPILE_OPTIONS "-mavx2")
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/gf2x/gf2x_mul_base_pclmul.c PROPERTIES COMPILE_OPTIONS "-mpclmul;")

##########################################################################################
# AVX512 OPTIMIZATION FLAGS
##########################################################################################
# Default behavior disabled for a fair comparison
option(USE_AVX512 "Enable AVX512 optimized code paths" OFF)
set(AVX512_FLAGS "-mavx512f;-mavx512bw;-mavx512dq")

if(USE_AVX512)
    add_definitions(-DAVX512)
    FILE(GLOB_RECURSE AVX512_SRCS ${PROJECT_SOURCE_DIR}/src/*_avx512.c)
    set_source_files_properties(${AVX512_SRCS} PROPERTIES COMPILE_OPTIONS "${AVX512_FLAGS}")
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/gf2x/gf2x_mul_base_vpclmul.c PROPERTIES COMPILE_OPTIONS "-mvpclmulqdq;${AVX512_FLAGS}")
else()
    set(AVX512_SRCS "")
endif()

##########################################################################################
# DECODER FLAGS
##########################################################################################

# Defualt decoder 
set(DECODER "BFMAX" CACHE STRING "Decoder implementation to use")
set_property(CACHE DECODER PROPERTY STRINGS "BGF" "BFMAX" "OOP" "HYBRID")
# All possible decoders
set(VALID_DECODERS "BGF" "BFMAX" "OOP" "HYBRID" "EXOTIC")
list(FIND VALID_DECODERS "${DECODER}" decoder_index)
if(decoder_index EQUAL -1)
    message(FATAL_ERROR "DECODER must be one of: ${VALID_DECODERS}")
endif()

add_definitions(-DDECODER_${DECODER})

# cmake variables with prefix "COMPILER_DEFINE_" are used to define compile-time constants
get_cmake_property(CACHE_VARIABLES CACHE_VARIABLES)
foreach(VAR ${CACHE_VARIABLES})
    if(VAR MATCHES "^COMPILER_DEFINE_(.+)$")
        set(NAME "${CMAKE_MATCH_1}")
        add_compile_definitions("${NAME}=${${VAR}}")
    endif()
endforeach()

set(R_BITS "" CACHE STRING "Override R_BITS (leave empty to use LEVEL/RELIABILITY_LEVEL default)")

if(R_BITS)
    add_compile_definitions(R_BITS=${R_BITS})
endif()