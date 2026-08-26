CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17

COMMON_SOURCES = ../chvatal.cpp ../common-functions.cpp ../instance.cpp
COMMON_HEADERS = ../chvatal.hpp ../common-functions.hpp ../instance.hpp ../dlx.cpp
BIN_DIR = bin
VARIANTS = \
	dlx_fixing_local \
	dlx_strong_branching \
	dlx_residual_bound \
	dlx_candidate_ordering \
	dlx_unit_propagation
BINARIES = $(addprefix $(BIN_DIR)/,$(VARIANTS))

.PHONY: all
all: $(BIN_DIR)/dlx_v2 $(BINARIES)

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/dlx_v2: ../dlx_v2_sem_restart.cpp $(COMMON_SOURCES) $(COMMON_HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) ../dlx_v2_sem_restart.cpp $(COMMON_SOURCES) -o $@

$(BIN_DIR)/%: %.cpp $(COMMON_SOURCES) $(COMMON_HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< $(COMMON_SOURCES) -o $@
