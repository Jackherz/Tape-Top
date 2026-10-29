# Tape-Top DSP — no dependencies, no CMake required.
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wshadow -Iinclude
OUT      := out

all: $(OUT)/tape_demo $(OUT)/engine_test

$(OUT)/tape_demo: src/demo.cpp include/tt/TapeMachine.h include/tt/Dsp.h | $(OUT)
	$(CXX) $(CXXFLAGS) src/demo.cpp -o $@

$(OUT)/engine_test: src/engine_test.cpp include/tt/TapeMachine.h include/tt/Dsp.h | $(OUT)
	$(CXX) $(CXXFLAGS) src/engine_test.cpp -o $@

$(OUT):
	mkdir -p $(OUT)

test: $(OUT)/engine_test
	./$(OUT)/engine_test

render: $(OUT)/tape_demo
	cd $(CURDIR) && ./$(OUT)/tape_demo && cp $(OUT)/*.wav ../ui/audio/

clean:
	rm -rf $(OUT)

.PHONY: all test render clean
