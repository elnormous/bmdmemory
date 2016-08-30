ifndef platform
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
endif

ifndef SDK_PATH
    ifeq ($(platform),linux)
        SDK_PATH=BlackmagicDeckLinkSDK/Linux/include
    else ifeq ($(platform),macos)
        SDK_PATH=BlackmagicDeckLinkSDK/Mac/include
    else ifeq ($(platform),windows)
        SDK_PATH=BlackmagicDeckLinkSDK/Win/include
    endif
endif

CXXFLAGS=-c -std=c++11 -Wall -I $(SDK_PATH)
LDFLAGS=-lpthread -ldl
ifeq ($(platform),macos)
LDFLAGS+=-framework CoreFoundation
endif

SOURCES=$(SDK_PATH)/DeckLinkAPIDispatch.cpp \
	src/main.cpp \
	src/BMDMemory.cpp
OBJECTS=$(SOURCES:.cpp=.o)

BINDIR=./bin
EXECUTABLE=bmdmemory

all: directories $(SOURCES) $(EXECUTABLE)

debug: CXXFLAGS+=-DDEBUG -g
debug: directories $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $(BINDIR)/$@

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean

clean:
	rm -rf src/*.o $(BINDIR)/$(EXECUTABLE) $(BINDIR)

directories: ${BINDIR}

${BINDIR}: 
	mkdir -p ${BINDIR}
