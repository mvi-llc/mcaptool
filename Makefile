# Whether to turn compiler warnings into errors
export WERROR ?= true
export BUILD_DIR ?= build
export CPPSTD ?= 20

default: release

release:
	conan install . -s compiler.cppstd=$(CPPSTD) --output-folder=build --build=missing
	cd ./$(BUILD_DIR) && bash -c "source conanbuild.sh && cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DWERROR=$(WERROR) && VERBOSE=1 cmake --build ."

debug:
	conan install . -s compiler.cppstd=$(CPPSTD) --output-folder=build --build=missing
	cd ./$(BUILD_DIR) && bash -c "source conanbuild.sh && cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DWERROR=$(WERROR) && VERBOSE=1 cmake --build ."

clean:
	rm -rf ./$(BUILD_DIR)
	# remove remains from running 'make coverage'
	rm -f *.profraw
	rm -f *.profdata

format:
	./scripts/format.sh

.PHONY: format
