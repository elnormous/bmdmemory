ifeq ($(OS),Windows_NT)
    platform=windows
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Linux)
        platform=linux
    endif
    ifeq ($(UNAME),Darwin)
        platform=macos
    endif
endif

CXXFLAGS=-c -std=c++11 -Wall -DLOG_SYSLOG -I $(SDK_PATH)
LDFLAGS=-lpthread -ldl

ifndef SDK_PATH
    ifeq ($(platform),linux)
        SDK_PATH=BlackmagicDeckLinkSDK/Linux/include
        LDFLAGS+=-lrt
    else ifeq ($(platform),macos)
        SDK_PATH=BlackmagicDeckLinkSDK/Mac/include
        LDFLAGS+=-framework CoreFoundation
    else ifeq ($(platform),windows)
        SDK_PATH=BlackmagicDeckLinkSDK/Win/include
    endif
endif

SOURCES=$(SDK_PATH)/DeckLinkAPIDispatch.cpp \
	src/main.cpp \
	src/BMDMemory.cpp \
	src/Log.cpp
OBJECTS=$(SOURCES:.cpp=.o)

BINDIR=./bin
EXECUTABLE=bmdmemory

all: CXXFLAGS+=-Os
all: directories $(SOURCES) $(EXECUTABLE)

debug: CXXFLAGS+=-DDEBUG -g -O0
debug: directories $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $(BINDIR)/$@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

prefix=/usr/bin

install:
	mkdir -p $(prefix)
	install -m 0755 $(BINDIR)/$(EXECUTABLE) $(prefix)

.PHONY: install

uninstall:
	rm -f $(prefix)/$(EXECUTABLE)

.PHONY: uninstall

clean:
	rm -rf src/*.o $(BINDIR)/$(EXECUTABLE) $(BINDIR)

.PHONY: clean

directories: ${BINDIR}

${BINDIR}: 
	mkdir -p ${BINDIR}
