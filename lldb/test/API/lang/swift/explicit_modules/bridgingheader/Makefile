SWIFT_SOURCES := main.swift
SWIFT_ENABLE_EXPLICIT_MODULES := Yes
SWIFT_BRIDGING_HEADER := bridging.h
SWIFTFLAGS_EXTRAS = -Xcc -I$(BUILDDIR)/secret

all: secret.h $(EXE)

include Makefile.rules

.PHONY: secret.h

secret.h:
	$(call MKDIR_P,$(BUILDDIR)/secret)
	$(call ECHO_TO_FILE,'struct S { int i; };',$(BUILDDIR)/secret/secret.h)
