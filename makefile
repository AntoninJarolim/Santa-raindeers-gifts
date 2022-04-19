TARGET=proj2

default: all

all:
	gcc $(TARGET).c -Wall -Wextra  -g -lpthread -lrt -o $(TARGET)

run: all
	./$(TARGET) 20 5 0 50

test: all run
	 cat proj2.out | ./check_output.sh

cat:
	cat proj2.out
