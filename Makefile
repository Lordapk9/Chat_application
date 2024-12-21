all:
	gcc -o client Chat_application.c -lpthread

clean:
	rm -rf client server