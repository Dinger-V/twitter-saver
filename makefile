.PHONY: clean

saver: twittersaver.c
	gcc -o saver twittersaver.c -lcurl

clean:
	rm -f saver