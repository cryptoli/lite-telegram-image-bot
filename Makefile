CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread
LDFLAGS = -lcurl

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
	rm -f $(TARGET) $(OBJ)

.PHONY: clean