KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

MODULE_NAME := kernel_alloc


SRC_DIR := src

BUILD_DIR := $(PWD)/build
BUILD_FILES += $(PWD)/*.ko
SOURCES := $(SRC_DIR)/*.c


CLANG_FORMAT_VER ?= 19
CLANG_FORMAT := clang-format-$(CLANG_FORMAT_VER)
CLANG_FORMAT_FLAGS += -i
FORMAT_FILES := $(SRC_DIR)/*.c

$(shell mkdir -p $(BUILD_DIR))



kbuild:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	mv $(BUILD_FILES) $(BUILD_DIR)

format:
	$(CLANG_FORMAT) $(CLANG_FORMAT_FLAGS) $(FORMAT_FILES)

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rmdir $(BUILD_DIR)

check:
	checker/check.sh

.PHONY: kbuild format clean check
