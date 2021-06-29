ALL_FILES = home_html.c home_html.h

all: ${ALL_FILES}

home_html.c home_html.h: home.html mkhtmlfile
	./mkhtmlfile $<

clean:
	rm -f ${ALL_FILES}
