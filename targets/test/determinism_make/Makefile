#
# Determinism harness build (POSIX side: macOS, Linux).
#
# Builds a small standalone binary that exercises the
# invariant-critical primitives (Fixed math, sphere math, Nuke apogee
# profile) for cross-OS bit-identity testing.  See
# source/test/determinism_main.cpp for the rationale.
#
# Usage from the repo root:
#
#   cd targets/test/determinism_make
#   make
#   ./determinism_test surface 10000 /tmp/surface.trace
#   ./determinism_test mirv    10000 /tmp/mirv.trace
#   ./determinism_test radar   10000 /tmp/radar.trace
#

REPO    := ../../..
CXX     ?= g++
CXXFLAGS ?= -O2 -Wall -Wno-write-strings -Wno-unused-parameter \
            -Wno-sign-compare -Wno-narrowing -fpermissive \
            -Wno-permissive
#
# fixed_64.h gates its UInt128 struct on __BIG_ENDIAN__ /
# __LITTLE_ENDIAN__.  Apple SDK headers pre-define these; Linux GCC
# does not.  Force little-endian explicitly (target hosts are all LE:
# Win x86, macOS x86/ARM, Linux x86/ARM all little-endian for our
# matrix).
#
DEFINES  := -DFIXED64_NUMERICS -D__LITTLE_ENDIAN__

# Include search order: the determinism_make stub layer first so that
# `#include "lib/universal_include.h"` resolves to our minimal shim
# instead of the engine's full header.
INCLUDES := \
    -I.                                        \
    -I$(REPO)/source                           \
    -I$(REPO)/source/test                      \
    -I$(REPO)/contrib/systemIV                 \
    -I$(REPO)/contrib/systemIV/lib

SRCS := \
    $(REPO)/source/test/determinism_main.cpp     \
    $(REPO)/source/test/determinism_harness.cpp  \
    $(REPO)/source/world/sphere.cpp              \
    $(REPO)/contrib/systemIV/lib/math/fixed_64.cpp

OBJS := $(SRCS:.cpp=.o)

TARGET := determinism_test

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SRCS) -o $@

run: $(TARGET)
	./$(TARGET) surface 10000 /tmp/surface.trace
	./$(TARGET) mirv    10000 /tmp/mirv.trace
	./$(TARGET) radar   10000 /tmp/radar.trace

clean:
	rm -f $(TARGET) $(OBJS)
