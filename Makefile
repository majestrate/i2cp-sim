
I2PD_SRC = $(wildcard i2pd/*.cpp)
SIMSERV_SRC = $(wildcard simserv/*.cpp)

OBJ = $(I2PD_SRC:.cpp=.o)
OBJ += $(SIMSERV_SRC:.cpp=.o)

LIBS = -lcrypto -lssl -lz -lboost_system -lboost_date_time -lboost_filesystem -lboost_program_options -lpthread


CXXFLAGS = -I. -Wall -std=c++11
CXX = clang++

BIN=simserv

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) -o $(BIN) $(OBJ) $(LIBS)
