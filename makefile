.PHONY: clean

saver: twittersaver.c platform.h
	gcc -o saver twittersaver.c -lcurl

clean:
	rm -f saver
