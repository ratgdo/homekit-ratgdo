HOMEKIT_PATH ?= $(abspath $(shell pwd)/../..)

PROJECT_NAME := homekit-ratgdo
EXTRA_COMPONENT_DIRS += $(HOMEKIT_PATH)/components/
EXTRA_COMPONENT_DIRS += $(HOMEKIT_PATH)/components/homekit
EXTRA_COMPONENT_DIRS += $(PROJECT_PATH)/src/
EXTRA_COMPONENT_DIRS += $(PROJECT_PATH)/components/esp8266-rtos-softuart/components

include $(IDF_PATH)/make/project.mk
