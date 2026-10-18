.PHONY: test compile

CXX ?= g++
TEST_BINARY ?= /tmp/autonomous-desk-motion-tests

# Only simulated UART data. No connection to a desk.
test:
	$(CXX) -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g tests/test_desk_motion.cpp -o $(TEST_BINARY)
	ASAN_OPTIONS=detect_leaks=0 $(TEST_BINARY)

# Dummy credentials. No upload or device connection.
compile:
	esphome compile tests/compile-test.yaml
