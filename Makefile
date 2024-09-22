CXX = g++

# 开启优化选项 -O2 和并行编译的预处理指令
CXXFLAGS = -std=c++17 -Wall -O3 -g -I$(INCDIR)

ifeq ($(OS),Windows_NT)
    CXXFLAGS += -pthread -I/mingw64/include
    LDFLAGS = -L/mingw64/lib -pthread -lcurl -lssl -lcrypto -lws2_32 -lsqlite3 -lz
    RM = cmd /C del /Q
else
    CXXFLAGS += -pthread
    LDFLAGS = -pthread -lcurl -lssl -lcrypto -lsqlite3 -lz
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
