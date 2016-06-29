all:
	$(CC) -std=c99 -O3 -o client *.c -lX11 -lXrandr -lm
