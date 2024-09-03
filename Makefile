CXX = g++

CXXFLAGS = -std=c++11 -Wall -I$(INCDIR)

ifeq ($(OS),Windows_NT)
    CXXFLAGS += -pthread -I/mingw64/include
    LDFLAGS = -L/mingw64/lib -lcurl -lssl -lcrypto -lws2_32 -pthread
    RM = cmd /C del /Q
else
    CXXFLAGS += -pthread
    LDFLAGS = -lcurl -lssl -lcrypto -pthread
    RM = rm -f
endif

TARGET = telegram_bot
SRCDIR = src
INCDIR = include

SRC = $(wildcard $(SRCDIR)/*.cpp)
OBJ = $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

clean:
	$(RM) $(TARGET) $(OBJ)

.PHONY: clean
