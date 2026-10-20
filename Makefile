CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17 -Iinclude
LDFLAGS = -lasound -lpthread

TARGET = OAPW_Player
# RACEDspEngine.cpp zu den Source-Dateien hinzugefügt
SRCS = src/main.cpp src/AlsaCapture.cpp src/AlsaPlayback.cpp src/RACEDspEngine.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
