CC = gcc

LIBS = -luv -lhiredis -lmongoc-1.0
FLAGS = -I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0 -g

all: main.o buffer.o redis.o mongo.o durotan.o draka.o
	@$(CC) -o sylvanas main.o buffer.o redis.o mongo.o durotan.o draka.o $(FLAGS) $(LIBS)

main.o: main.c
	@$(CC) -c main.c $(FLAGS)

buffer.o: buffer.c
	@$(CC) -c buffer.c $(FLAGS)

redis.o: redis.c
	@$(CC) -c redis.c $(FLAGS)

mongo.o: mongo.c
	@$(CC) -c mongo.c $(FLAGS)

durotan.o: durotan.c
	@$(CC) -c durotan.c $(FLAGS)

draka.o: draka.c
	@$(CC) -c draka.c $(FLAGS)
