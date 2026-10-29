BUILD_DIR ?= build
BIN ?= syspulse

.PHONY: all build run clean test

all: build

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && $(MAKE) -j$$(nproc)
	@cp $(BUILD_DIR)/$(BIN) ./$(BIN)

run: build
	./$(BIN)

test: build
	./$(BIN) --test

clean:
	@rm -rf $(BUILD_DIR) $(BIN)
