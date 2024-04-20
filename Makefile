CC	=	gcc
CFLAGS	=	-std=c11	-pedantic	-pthread
all	:	crawler
crawler	:	crawler.c
	$(CC)	$(CFLAGS)	crawler.c	-o	crawler	-lgumbo	-lcurl
clean	:
	rm 	-f	crawler
run	:
	./crawler	http://momoreayinde.dev	1