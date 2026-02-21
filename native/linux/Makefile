CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -I../common
LDFLAGS  = -lpulse-simple -lpulse -lpthread
TARGET   = vis-capture
SRC      = main.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../common/protocol.h ../common/fft.h ../common/ws_server.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
