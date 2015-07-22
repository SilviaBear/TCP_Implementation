all: sender receiver
sender:
	gcc -g -pthread -o reliable_sender sender_main.c
receiver:
	gcc -g -pthread -o reliable_receiver receiver_main.c
clean:
	rm reliable_sender reliable_receiver *.o
