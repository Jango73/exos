################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

# Architecture selection (default to x86-32)
ARCH ?= x86-32

ifeq ($(ARCH),x86-32)
# Using x86-32 cross-toolchain
else ifeq ($(ARCH),x86-64)
# Using host toolchain for x86-64 builds
else
$(error Unsupported architecture $(ARCH))
endif

BUILD_CORE_NAME ?=
BUILD_IMAGE_NAME ?=

SUBMAKE = $(MAKE) ARCH=$(ARCH) BUILD_CORE_NAME=$(BUILD_CORE_NAME) BUILD_IMAGE_NAME=$(BUILD_IMAGE_NAME)

################################################################################

.PHONY: all clean hello portal tictactoe terminal_tactics netget input-info memory_smoke master slave module_host module_sample module_import pipe_source pipe_upper pipe_summary tcc tinygl_demo

SYSTEM_PROGRAMS = hello portal tictactoe terminal_tactics netget input-info memory_smoke master slave module_host module_sample module_import pipe_source pipe_upper pipe_summary tcc tinygl_demo

all: $(SYSTEM_PROGRAMS)

################################################################################
# Hello program

hello:
	@echo "[ Building hello ]"
	+$(SUBMAKE) -C test/hello all

hello_clean:
	+$(SUBMAKE) -C test/hello clean

################################################################################
# Portal program

portal:
	@echo "[ Building portal ]"
	+$(SUBMAKE) -C portal all

portal_clean:
	+$(SUBMAKE) -C portal clean

################################################################################
# TicTacToe program

tictactoe:
	@echo "[ Building tictactoe ]"
	+$(SUBMAKE) -C tictactoe all

tictactoe_clean:
	+$(SUBMAKE) -C tictactoe clean

################################################################################
# Terminal Tactics program

terminal_tactics:
	@echo "[ Building terminal tactics ]"
	+$(SUBMAKE) -C terminal-tactics all

terminal_tactics_clean:
	+$(SUBMAKE) -C terminal-tactics clean

################################################################################
# NetGet program

netget:
	@echo "[ Building netget ]"
	+$(SUBMAKE) -C netget all

netget_clean:
	+$(SUBMAKE) -C netget clean

################################################################################
# Input info program

input-info:
	@echo "[ Building input-info ]"
	+$(SUBMAKE) -C input-info all

input-info_clean:
	+$(SUBMAKE) -C input-info clean

################################################################################
# Memory smoke program

memory_smoke:
	@echo "[ Building memory-stress ]"
	+$(SUBMAKE) -C test/memory-stress all

memory_smoke_clean:
	+$(SUBMAKE) -C test/memory-stress clean

################################################################################
# Master test program

master:
	@echo "[ Building master ]"
	+$(SUBMAKE) -C test/master all

master_clean:
	+$(SUBMAKE) -C test/master clean

################################################################################
# Slave test program

slave:
	@echo "[ Building slave ]"
	+$(SUBMAKE) -C test/slave all

slave_clean:
	+$(SUBMAKE) -C test/slave clean

################################################################################
# Module host test program

module_host:
	@echo "[ Building module host ]"
	+$(SUBMAKE) -C test/module-host all

module_host_clean:
	+$(SUBMAKE) -C test/module-host clean

################################################################################
# Module sample

module_sample:
	@echo "[ Building module sample ]"
	+$(SUBMAKE) -C test/module-sample all

module_sample_clean:
	+$(SUBMAKE) -C test/module-sample clean

################################################################################
# Module import

module_import:
	@echo "[ Building module import ]"
	+$(SUBMAKE) -C test/module-import all

module_import_clean:
	+$(SUBMAKE) -C test/module-import clean

################################################################################
# Pipe source test program

pipe_source:
	@echo "[ Building pipe source ]"
	+$(SUBMAKE) -C test/pipe-source all

pipe_source_clean:
	+$(SUBMAKE) -C test/pipe-source clean

################################################################################
# Pipe upper test program

pipe_upper:
	@echo "[ Building pipe upper ]"
	+$(SUBMAKE) -C test/pipe-upper all

pipe_upper_clean:
	+$(SUBMAKE) -C test/pipe-upper clean

################################################################################
# Pipe summary test program

pipe_summary:
	@echo "[ Building pipe summary ]"
	+$(SUBMAKE) -C test/pipe-summary all

pipe_summary_clean:
	+$(SUBMAKE) -C test/pipe-summary clean

################################################################################
# TinyCC bootstrap compiler

tcc:
	@echo "[ Building tcc ]"
	+$(SUBMAKE) -C tcc all

tcc_clean:
	+$(SUBMAKE) -C tcc clean

################################################################################
# TinyGL Demo

tinygl_demo:
	@echo "[ Building tinygl-demo ]"
	+$(SUBMAKE) -C tinygl-demo all

tinygl_demo_clean:
	+$(SUBMAKE) -C tinygl-demo clean

################################################################################

clean: hello_clean portal_clean tictactoe_clean terminal_tactics_clean netget_clean input-info_clean memory_smoke_clean master_clean slave_clean module_host_clean module_sample_clean module_import_clean pipe_source_clean pipe_upper_clean pipe_summary_clean tcc_clean tinygl_demo_clean
	@echo "[ Cleaning system programs ]"
