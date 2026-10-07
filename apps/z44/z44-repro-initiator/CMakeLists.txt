cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(z44_repro_initiator)
target_sources(app PRIVATE src/main.c)

# Control build: cmake -DCREDITS_IN_REQUEST=1 (default off keeps the image unchanged)
if(CREDITS_IN_REQUEST)
  target_compile_definitions(app PRIVATE CREDITS_IN_REQUEST)
endif()
