crow : crow.c
	gcc crow.c -o crow

crow.tar.gz : crow
	rm -f crow.tar.gz
	tar --transform 's,^,crow/,' -czf crow.tar.gz \
	Makefile \
	crow.c
	
tar : crow.tar.gz

install : crow
	cp crow /usr/bin


