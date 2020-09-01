cmake_minimum_required(VERSION 3.8)
project(libfev LANGUAGES ASM C CXX)

set(FEV_VERSION_MAJOR 0)
set(FEV_VERSION_MINOR 1)
set(FEV_VERSION_PATCH 0)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Target architecture

include(TargetArch)

target_architecture(FEV_ARCH)
if(FEV_ARCH STREQUAL x86_64)
  set(FEV_ARCH_X86_64 1)
elseif(FEV_ARCH STREQUAL i386)
  set(FEV_ARCH_I386 1)
endif()
unset(FEV_ARCH)

if(NOT (FEV_ARCH_X86_64 OR FEV_ARCH_I386))
  message(FATAL_ERROR "Target architecture is unsupported. Currently only x86_64 and i386 are supported.")
endif()

# Compiler

if(CMAKE_C_COMPILER_ID MATCHES Clang)
  set(FEV_COMPILER_CLANG 1)
elseif(CMAKE_COMPILER_IS_GNUCC)
  set(FEV_COMPILER_GCC 1)
endif()

if(NOT (FEV_COMPILER_CLANG OR FEV_COMPILER_GCC))
  message(WARNING "Your compiler is unsupported. If it fails to compile, switch to Clang or GCC.")
endif()

# Operating system

if(APPLE)
  if(CMAKE_SYSTEM_NAME MATCHES MacOS)
    set(FEV_OS_MACOS 1)
  elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
    set(FEV_OS_DARWIN 1)
  endif()
elseif(UNIX)
  if(CMAKE_SYSTEM_NAME MATCHES Linux)
    set(FEV_OS_LINUX 1)
  elseif(CMAKE_SYSTEM_NAME MATCHES DragonFlyBSD)
    set(FEV_OS_DRAGONFLYBSD 1)
  elseif(CMAKE_SYSTEM_NAME MATCHES FreeBSD)
    set(FEV_OS_FREEBSD 1)
  elseif(CMAKE_SYSTEM_NAME MATCHES NetBSD)
    set(FEV_OS_NETBSD 1)
  elseif(CMAKE_SYSTEM_NAME MATCHES OpenBSD)
    set(FEV_OS_OPENBSD 1)
  endif()
elseif(WIN32)
  set(FEV_OS_WIN 1)
endif()

if(FEV_OS_DRAGONFLYBSD OR FEV_OS_FREEBSD OR FEV_OS_NETBSD OR FEV_OS_OPENBSD)
  set(FEV_OS_BSD 1)
endif()

if(FEV_OS_MACOS OR FEV_OS_DARWIN OR FEV_OS_LINUX OR FEV_OS_BSD)
  set(FEV_OS_POSIX 1)
endif()

if(NOT FEV_OS_POSIX)
  message(FATAL_ERROR "Your operating system is unsupported. Currently only POSIX systems are supported.")
endif()

# Detect functions

include(CheckFunctionExists)

check_function_exists(accept4 FEV_HAVE_ACCEPT4)

# Options

option(FEV_BUILD_EXAMPLES "Build examples" OFF)
option(FEV_BUILD_TESTS "Build tests" OFF)

option(FEV_ENABLE_SUGGEST_ATTRIBUTES_WARNINGS "Enable suggest attributes warnings" OFF)
option(FEV_ENABLE_DEBUG_ASSERT "Enable debug assert macro" OFF)
option(FEV_ENABLE_STATIC_ANALYSIS "Enable static analysis" OFF)
option(FEV_ENABLE_LTO "Enable link time optimization" OFF)
option(FEV_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(FEV_ENABLE_MEMSAN "Enable Memory Sanitizer" OFF)
option(FEV_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

option(FEV_ASSUME_MALLOC_NEVER_FAILS "Assume that malloc() never fails" OFF)

set(FEV_PAGE_SIZE 4096 CACHE STRING "Page size")
set(FEV_DCACHE_LINE_SIZE 64 CACHE STRING "Data cache line size")
set(FEV_ICACHE_LINE_SIZE 64 CACHE STRING "Instruction cache line size")

set(FEV_DEFAULT_STACK_SIZE 65536 CACHE STRING "Default stack size (accessible stack without guard)")
set(FEV_DEFAULT_GUARD_SIZE 0 CACHE STRING "Default guard size")

set(FEV_SCHED work-stealing-locking CACHE STRING "Scheduler; possible values: work-sharing-locking/work-sharing-bounded-mpmc/work-sharing-simple-mpmc/work-stealing-locking/work-stealing-bounded-mpmc/work-stealing-bounded-spmc")
set_property(CACHE FEV_SCHED PROPERTY STRINGS
  work-sharing-locking
  work-sharing-bounded-mpmc
  work-sharing-simple-mpmc
  work-stealing-locking
  work-stealing-bounded-mpmc
  work-stealing-bounded-spmc)

if(FEV_SCHED STREQUAL work-sharing-bounded-mpmc)
  set(FEV_SCHED_SHR_BOUNDED_MPMC_QUEUE_CAPACITY 1024 CACHE STRING "Bounded MPMC queue capacity for run queue entries")
elseif(FEV_SCHED STREQUAL work-sharing-simple-mpmc)
  set(FEV_SCHED_SHR_SIMPLE_MPMC_LOCAL_POOL_SIZE 128 CACHE STRING "Local (per worker) pool size for run queue entries")
elseif(FEV_SCHED STREQUAL work-stealing-locking)
  set(FEV_SCHED_STEAL_LOCKING_LOCK mutex CACHE STRING "Run queue lock; possible values: mutex/spinlock")
  set_property(CACHE FEV_SCHED_STEAL_LOCKING_LOCK PROPERTY STRINGS mutex spinlock)
  set(FEV_SCHED_STEAL_LOCKING_STEAL_COUNT 8 CACHE STRING "Number of fibers to steal")
elseif(FEV_SCHED STREQUAL work-stealing-bounded-mpmc)
  set(FEV_SCHED_STEAL_BOUNDED_MPMC_QUEUE_CAPACITY 256 CACHE STRING "Per worker bounded MPMC queue capacity for run queue entries")
  set(FEV_SCHED_STEAL_BOUNDED_MPMC_STEAL_COUNT 8 CACHE STRING "Number of fibers to steal")
elseif(FEV_SCHED STREQUAL work-stealing-bounded-spmc)
  set(FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY 256 CACHE STRING "Per worker bounded SPMC queue capacity for run queue entries")
  set(FEV_SCHED_STEAL_BOUNDED_SPMC_STEAL_COUNT 8 CACHE STRING "Number of fibers to steal")
endif()

if(FEV_OS_MACOS OR FEV_OS_DARWIN OR FEV_OS_BSD)
  set(FEV_POLLER kqueue CACHE STRING "Poller; possible values: kqueue")
  set_property(CACHE FEV_POLLER PROPERTY STRINGS kqueue)
elseif(FEV_OS_LINUX)
  set(FEV_POLLER epoll CACHE STRING "Poller; possible values: epoll/io_uring")
  set_property(CACHE FEV_POLLER PROPERTY STRINGS epoll io_uring)
endif()

if(FEV_POLLER STREQUAL io_uring)
  set(FEV_IO_URING_ENTRIES_PER_WORKER 1024 CACHE STRING "Number of IO ring entries per worker")
endif()

set(FEV_TIMERS binheap CACHE STRING "Timers; possible values: binheap/rbtree")
set_property(CACHE FEV_TIMERS PROPERTY STRINGS binheap rbtree)

set(FEV_TIMERS_MIN_LOCK mutex CACHE STRING "Timer's min lock; possible values: mutex/spinlock")
set_property(CACHE FEV_TIMERS_MIN_LOCK PROPERTY STRINGS mutex spinlock)

set(FEV_ILOCK_LOCK mutex CACHE STRING "Internal lock's lock; possible values: mutex/spinlock")
set_property(CACHE FEV_ILOCK_LOCK PROPERTY STRINGS mutex spinlock)

if(FEV_ILOCK_LOCK STREQUAL mutex)
  set(FEV_ILOCK_LOCK_MUTEX ON)
elseif(FEV_ILOCK_LOCK STREQUAL spinlock)
  set(FEV_ILOCK_LOCK_SPINLOCK ON)
else()
  message(FATAL_ERROR "Invalid FEV_ILOCK_LOCK value")
endif()

# Common source files

set(FEV_SOURCES
  src/fev_alloc.c
  src/fev_cond.c
  src/fev_fiber.c
  src/fev_fiber_attr.c
  src/fev_ilock.c
  src/fev_mutex.c
  src/fev_sem.c
)

# Context

list(APPEND FEV_SOURCES src/fev_context_x86_family.c)
if(FEV_ARCH_I386)
  list(APPEND FEV_SOURCES src/fev_context_i386.S)
elseif(FEV_ARCH_X86_64)
  if(FEV_OS_POSIX)
    list(APPEND FEV_SOURCES src/fev_context_x86_64_posix.S)
  else()
    message(FATAL_ERROR "Not implemented")
  endif()
endif()

# Fiber's stack

if(FEV_OS_POSIX)
  list(APPEND FEV_SOURCES src/fev_stack_posix.c)
else()
  message(FATAL_ERROR "Not implemented")
endif()

# Scheduler

list(APPEND FEV_SOURCES src/fev_sched_attr.c src/fev_sched_common.c)
if(FEV_SCHED STREQUAL "work-sharing-locking")
  set(FEV_SCHED_WORK_SHARING_LOCKING ON)
  list(APPEND FEV_SOURCES src/fev_sched_shr_locking.c)
elseif(FEV_SCHED STREQUAL "work-sharing-bounded-mpmc")
  set(FEV_SCHED_WORK_SHARING_BOUNDED_MPMC ON)
  list(APPEND FEV_SOURCES src/fev_sched_shr_bounded_mpmc.c)
elseif(FEV_SCHED STREQUAL "work-sharing-simple-mpmc")
  set(FEV_SCHED_WORK_SHARING_SIMPLE_MPMC ON)
  list(APPEND FEV_SOURCES src/fev_sched_shr_simple_mpmc.c)
elseif(FEV_SCHED STREQUAL "work-stealing-locking")
  set(FEV_SCHED_WORK_STEALING_LOCKING ON)
  if(FEV_SCHED_STEAL_LOCKING_LOCK STREQUAL mutex)
    set(FEV_SCHED_STEAL_LOCKING_LOCK_MUTEX ON)
  elseif(FEV_SCHED_STEAL_LOCKING_LOCK STREQUAL spinlock)
    set(FEV_SCHED_STEAL_LOCKING_LOCK_SPINLOCK ON)
  else()
    message(FATAL_ERROR "Invalid FEV_SCHED_STEAL_LOCKING_LOCK value")
  endif()
  list(APPEND FEV_SOURCES src/fev_sched_steal_locking.c)
elseif(FEV_SCHED STREQUAL "work-stealing-bounded-mpmc")
  set(FEV_SCHED_WORK_STEALING_BOUNDED_MPMC ON)
  list(APPEND FEV_SOURCES src/fev_sched_steal_bounded_mpmc.c)
elseif(FEV_SCHED STREQUAL "work-stealing-bounded-spmc")
  set(FEV_SCHED_WORK_STEALING_BOUNDED_SPMC ON)
  list(APPEND FEV_SOURCES src/fev_sched_steal_bounded_spmc.c)
else()
  message(FATAL_ERROR "Invalid FEV_SCHED value")
endif()

# Poller

if(FEV_POLLER STREQUAL epoll)
  set(FEV_POLLER_EPOLL ON)
  list(APPEND FEV_SOURCES src/fev_poller_epoll.c src/fev_poller_reactor.c)
elseif(FEV_POLLER STREQUAL kqueue)
  set(FEV_POLLER_KQUEUE ON)
  list(APPEND FEV_SOURCES src/fev_poller_kqueue.c src/fev_poller_reactor.c)
elseif(FEV_POLLER STREQUAL io_uring)
  set(FEV_POLLER_IO_URING ON)
  list(APPEND FEV_SOURCES src/fev_poller_io_uring.c)
else()
  message(FATAL_ERROR "Invalid FEV_POLLER value")
endif()

# Timers

list(APPEND FEV_SOURCES src/fev_timers.c)
if(FEV_TIMERS STREQUAL binheap)
  set(FEV_TIMERS_BINHEAP ON)
  list(APPEND FEV_SOURCES src/fev_timers_binheap.c)
elseif(FEV_TIMERS STREQUAL rbtree)
  set(FEV_TIMERS_RBTREE ON)
  list(APPEND FEV_SOURCES src/fev_timers_rbtree.c)
else()
  message(FATAL_ERROR "Invalid FEV_TIMERS value")
endif()

if(FEV_TIMERS_MIN_LOCK STREQUAL mutex)
  set(FEV_TIMERS_MIN_LOCK_MUTEX ON)
elseif(FEV_TIMERS_MIN_LOCK STREQUAL spinlock)
  set(FEV_TIMERS_MIN_LOCK_SPINLOCK ON)
else()
  message(FATAL_ERROR "Invalid FEV_TIMERS_MIN_LOCK value")
endif()

# Socket

if(FEV_POLLER_IO_URING)
  list(APPEND FEV_SOURCES src/fev_socket_io_uring.c)
elseif(FEV_POLLER_EPOLL OR FEV_POLLER_KQUEUE)
  list(APPEND FEV_SOURCES src/fev_socket_reactor.c)
else()
  message(FATAL_ERROR "Not implemented")
endif()

# Threads

if(FEV_OS_POSIX)
  set(FEV_THR_POSIX ON)
  find_package(Threads REQUIRED)
  list(APPEND FEV_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
elseif(FEV_OS_WIN)
  message(FATAL_ERROR "Not implemented")
  # set(FEV_THR_WIN ON)
  # list(APPEND FEV_SOURCES src/fev_thr_win.c)
endif()

# Thread mutex

if(FEV_OS_LINUX)
  set(FEV_THR_MUTEX_LINUX ON)
  list(APPEND FEV_SOURCES src/fev_thr_mutex_linux.c)
elseif(FEV_OS_POSIX)
  set(FEV_THR_MUTEX_POSIX ON)
elseif(FEV_OS_WIN)
  set(FEV_THR_MUTEX_WIN ON)
endif()

# Thread semaphore

if(FEV_OS_MACOS OR FEV_OS_DARWIN)
  set(FEV_THR_SEM_MACOS ON)
  list(APPEND FEV_SOURCES src/fev_thr_sem_macos.c)
elseif(FEV_OS_POSIX)
  set(FEV_THR_SEM_POSIX ON)
elseif(FEV_OS_WIN)
  set(FEV_THR_SEM_WIN ON)
endif()

# OS

if(FEV_OS_POSIX)
  list(APPEND FEV_SOURCES src/fev_os_nix.c)
elseif(FEV_OS_WIN)
  list(APPEND FEV_SOURCES src/fev_os_win.c)
endif()

# Generate config file

configure_file(include/fev/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/fev/config.h")

# Build

add_library(fev ${FEV_SOURCES})

# ABI version
set_target_properties(fev
  PROPERTIES
    VERSION ${FEV_VERSION_MAJOR}.${FEV_VERSION_MINOR}.${FEV_VERSION_PATCH}
    SOVERSION ${FEV_VERSION_MAJOR}
)

target_include_directories(fev PUBLIC include)
target_include_directories(fev PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
target_include_directories(fev PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

target_link_libraries(fev PRIVATE ${FEV_LIBRARIES})

set_property(TARGET fev PROPERTY C_STANDARD 11)

function(fev_set_compile_options target)
  if(NOT (FEV_COMPILER_CLANG OR FEV_COMPILER_GCC))
    return()
  endif()

  target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion -Wnull-dereference -Wvla)
  target_compile_definitions(${target} PRIVATE -D_GNU_SOURCE)

  if(FEV_COMPILER_GCC AND FEV_ENABLE_SUGGEST_ATTRIBUTES_WARNINGS)
    target_compile_options(${target} PRIVATE -Wsuggest-attribute=const -Wsuggest-attribute=pure
                           -Wsuggest-attribute=noreturn -Wsuggest-attribute=format
                           -Wsuggest-attribute=malloc)
  endif()

  if(FEV_COMPILER_GCC)
    # GCC makes some bad guesses.
    target_compile_options(${target} PRIVATE -fno-guess-branch-probability)
  endif()

  if(FEV_ENABLE_STATIC_ANALYSIS)
    if(FEV_COMPILER_CLANG)
      target_compile_options(${target} PRIVATE --analyze)
    elseif(FEV_COMPILER_GCC)
      target_compile_options(${target} PRIVATE -fanalyzer)
    endif()
  endif()

  if(FEV_ENABLE_LTO)
    target_compile_options(${target} PRIVATE -flto)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -flto")
  endif()

  if(FEV_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fsanitize-address-use-after-scope)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=address")
  endif()

  if(FEV_ENABLE_MEMSAN)
    target_compile_options(${target} PRIVATE -fsanitize=memory)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=memory")
  endif()

  if(FEV_ENABLE_UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=undefined)
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -fsanitize=undefined")
  endif()
endfunction()

fev_set_compile_options(fev)

if(FEV_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(FEV_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# pkg-config file

configure_file(libfev.pc.in
  "${CMAKE_BINARY_DIR}/libfev.pc"
  @ONLY
)

# Install

include(GNUInstallDirs)

install(TARGETS fev
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
  "${CMAKE_SOURCE_DIR}/include/fev/fev.h"
  "${CMAKE_SOURCE_DIR}/include/fev/fev++.hpp"
  "${CMAKE_BINARY_DIR}/include/fev/config.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fev"
)

install(FILES
  "${CMAKE_BINARY_DIR}/libfev.pc"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/pkgconfig"
)
