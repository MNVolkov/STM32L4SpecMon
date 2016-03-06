#pragma once 

namespace mbed {

void mbed_set_unbuffered_stream(FILE *_file);
int mbed_getc(FILE *_file);
char* mbed_gets(char *s, int size, FILE *_file);

class Stream {
public:
	Stream(const char *name=NULL) {}
	int putc(int c) { return _putc(c); }
	int puts(const char *s) {
		int r;
		for (; *s; ++s) {
			if ((r = _putc(*s)) < 0) {
				return r;
			}
		}
		return 0;
	}
protected:
	virtual int _putc(int c) = 0;
};

}
