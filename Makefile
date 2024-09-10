CXX = g++

# 开启优化选项 -O2 和并行编译的预处理指令
CXXFLAGS = -std=c++11 -Wall -O2 -I$(INCDIR)

ifeq ($(OS),Windows_NT)
    CXXFLAGS += -pthread -I/mingw64/include
    LDFLAGS = -L/mingw64/lib -lcurl -lssl -lcrypto -lws2_32 -pthread -lsqlite3 -lz
    RM = cmd /C del /Q
else
    CXXFLAGS += -pthread
    LDFLAGS = -lcurl -lssl -lcrypto -pthread -lsqlite3 -lz
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
