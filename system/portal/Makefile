################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

APP_NAME := portal
APP_SOURCES := source/portal.c \
               source/portal-string.c \
               source/math/Math3D.c \
               source/ui/Button.c \
               source/ui/ClockWidget.c \
               source/ui/Cube3D.c \
               source/ui/DockHost.c \
               source/ui/Dockable.c \
               source/ui/LogViewer.c \
               source/ui/OnScreenDebugInfo.c \
               source/ui/ShellBar.c \
               source/ui/Startup-Desktop-Components.c \
               source/ui/WindowDockHost.c \
               source/ui/WindowDockable.c

APP_HEADERS := $(wildcard source/*.h) $(wildcard source/ui/*.h) $(wildcard source/math/*.h)
APP_EXTRA_CFLAGS := -Isource

include ../../runtime/make/exos.mk
