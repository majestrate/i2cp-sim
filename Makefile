
I2PD_SRC = $(wildcard i2pd/*.cpp)
SIMSERV_SRC = $(wildcard simserv/*.cpp)

OBJ = $(I2PD_SRC:.cpp=.o)
OBJ += $(SIMSERV_SRC:.cpp=.o)

LIBS = -lcrypto -lssl -lz -lboost_system -lboost_date_time -lboost_filesystem -lboost_program_options -lpthread


CXXFLAGS = -I. -g -Wall -Wextra -Wno-unused-parameter -pedantic -Wno-misleading-indentation -std=c++11

BIN=i2psim

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) -o $(BIN) $(OBJ) $(LIBS)

clean:
	rm -f $(OBJ) $(BIN)
