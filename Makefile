all: run #runwithdebugger

start:
	@sh ./zabij.sh
compile: start
	mpicc -Wall -o chlanie2 chlanie2.c
run: compile
	mpirun --oversubscribe -np 3 chlanie2
#runwithdebugger: compile
#	mpirun --oversubscribe -np 5 xterm -hold -e gdb -ex run --args chlanie2
