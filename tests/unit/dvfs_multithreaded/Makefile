SOURCES = dvfs_multithreaded.cc
TARGET = dvfs_multithreaded

THREADS ?= 64
NUM_ITERATIONS ?= 1
ITERATION_SIZE ?= 512
CACHE_LINE_SIZE ?= 64
MEMORY_LOW_FREQUENCY ?= 0.1
MEMORY_HIGH_FREQUENCY ?= 1.0
COMPUTE_LOW_FREQUENCY ?= 1.0
COMPUTE_HIGH_FREQUENCY ?= 1.0
APP_FLAGS ?= $(THREADS) $(NUM_ITERATIONS) $(ITERATION_SIZE) $(CACHE_LINE_SIZE) $(MEMORY_LOW_FREQUENCY) $(MEMORY_HIGH_FREQUENCY) $(COMPUTE_LOW_FREQUENCY) $(COMPUTE_HIGH_FREQUENCY)
CORES ?= $(THREADS)
include ../../Makefile.tests
