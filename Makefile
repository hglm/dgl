CFLAGS = -std=gnu++0x -Ofast -Wall -Wmissing-declarations
#CFLAGS = -ggdb -Wall
LIBRARY_OBJECT = libdgl.a
LIBRARY_MODULE_OBJECTS = dgl-main.o dgl-consolefb.o

PKG_CONFIG_CFLAGS_DEMO = datasetturbo
PKG_CONFIG_LIBS_DEMO = datasetturbo
HAVE_PIXMAN = $(shell if [ -e /usr/include/pixman-1/pixman.h ]; then echo YES; fi)
ifeq ($(HAVE_PIXMAN), YES)
DEFINES_LIB += -DDGL_USE_PIXMAN
PKG_CONFIG_CFLAGS_LIB += pixman-1
PKG_CONFIG_LIBS_DEMO += pixman-1
endif

CFLAGS_LIB = $(CFLAGS) $(DEFINES_LIB) `pkg-config --cflags $(PKG_CONFIG_CFLAGS_LIB)`

CFLAGS_DEMO = $(CFLAGS) `pkg-config --cflags $(PKG_CONFIG_CFLAGS_DEMO)`
LFLAGS_DEMO = `pkg-config --libs $(PKG_CONFIG_LIBS_DEMO)` -lpthread
DEMO_PROGRAM = test-dgl

TARGET_MACHINE := $(shell gcc -dumpmachine)
LIB_DIR = /usr/lib/$(TARGET_MACHINE)
HEADER_FILES = dgl.h

all : $(LIBRARY_OJECT) $(DEMO_PROGRAM) simple-example textmode

$(DEMO_PROGRAM) : $(LIBRARY_OBJECT) test-dgl.o
	g++ -o $(DEMO_PROGRAM) test-dgl.o $(LIBRARY_OBJECT) $(LFLAGS_DEMO)

simple-example : $(LIBRARY_OBJECT) simple-example.o
	g++ -o simple-example simple-example.o $(LIBRARY_OBJECT) \
		`pkg-config --libs pixman-1`

$(LIBRARY_OBJECT) : $(LIBRARY_MODULE_OBJECTS)
	ar r $(LIBRARY_OBJECT) $(LIBRARY_MODULE_OBJECTS)

install : libdgl.a textmode
	install -m 0644 $(LIBRARY_OBJECT) $(LIB_DIR)/$(LIBRARY_OBJECT)
	@for x in $(HEADER_FILES); do \
	echo Installing /usr/include/$$x.; \
	install -m 0644 $$x /usr/include/$$x; done
	install -m 0755 textmode /usr/bin

test-dgl.o : test-dgl.cpp
	g++ -c $(CFLAGS_DEMO) $< -o $@

.cpp.o :
	g++ -c $(CFLAGS_LIB) $< -o $@

clean :
	rm -f $(LIBRARY_MODULE_OBJECTS) $(LIBRARY_OBJECT)
	rm -f test-dgl.o $(DEMO_PROGRAM)

textmode : textmode.cpp
	g++ -O textmode.cpp -o textmode

dep : .depend

.depend : Makefile
	@echo Running make dep
	@rm -f .depend
	@gcc -MM $(patsubst %.o,%.cpp,$(LIBRARY_MODULE_OBJECTS)) >>.depend
	@# Make sure Makefile is a dependency for all modules.
	@for x in $(LIBRARY_MODULE_OBJECTS); do \
	echo $$x : Makefile >>.depend; done
	@gcc -MM test-dgl.cpp >>.depend
	@gcc -MM simple-example.cpp >>.depend
	@gcc -MM textmode.cpp >>.depend

include .depend
