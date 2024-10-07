CXX = g++
CXXFLAGS = -std=c++17 -Wall -O3 -g

INCDIR = include
INCLUDES = -I$(INCDIR) -I$(INCDIR)/cache -I$(INCDIR)/db -I$(INCDIR)/http -I$(INCDIR)/server -I$(INCDIR)/thread

CXXFLAGS += $(INCLUDES)

ifeq ($(OS),Windows_NT)
    CXXFLAGS += -I/mingw64/include
    LDFLAGS = -L/mingw64/lib -pthread -lcurl -lssl -lcrypto -lws2_32 -lsqlite3 -lz -lcrypt32
    RM = cmd /C del /Q
else
    LDFLAGS = -pthread -lcurl -lssl -lcrypto -lsqlite3 -lz
    RM = rm -f
endif

TARGET = telegram_bot
SRCDIR = src

SRC = $(wildcard $(SRCDIR)/*.cpp) $(wildcard $(SRCDIR)/cache/*.cpp) $(wildcard $(SRCDIR)/db/*.cpp) $(wildcard $(SRCDIR)/http/*.cpp) $(wildcard $(SRCDIR)/server/*.cpp) $(wildcard $(SRCDIR)/thread/*.cpp)
OBJ = $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(TARGET) $(OBJ)

.PHONY: clean
