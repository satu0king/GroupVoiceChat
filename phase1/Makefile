
BUILD   = ./build
BIN     = ./bin
INCLUDE = ./
SRC = ./

all: $(BIN)/server $(BIN)/client $(BIN)/rec $(BIN)/play
 
	
$(BIN)/server: $(SRC)/server.c $(INCLUDE)/common.h
	gcc $(SRC)/server.c -lpthread -lpulse -lpulse-simple -o $(BIN)/server -I $(INCLUDE)

$(BIN)/client: $(SRC)/client.c $(INCLUDE)/common.h
	gcc $(SRC)/client.c -lpthread -lpulse -lpulse-simple -o $(BIN)/client -I $(INCLUDE)

clean: 
	rm -rf $(BIN)/*

$(BIN)/play:
	gcc $(SRC)/play_sample.c -lpulse -lpulse-simple -o $(BIN)/play

$(BIN)/rec:
	gcc $(SRC)/rec_sample.c -lpulse -lpulse-simple -o $(BIN)/rec
