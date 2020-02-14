#include <stddef.h>
#include <stdint.h>


#define SEEK_SET	0	/* Seek from beginning of file.  */
#define SEEK_CUR	1	/* Seek from current position.  */
#define SEEK_END	2	/* Seek from end of file.  */
/*
	This is the syntax for using the asm() keyword in your C/C++ code:

	asm ( assembler template
		: output operands                   (optional)
		: input operands                    (optional)
		: clobbered registers list          (optional)
		);
*/

static inline void outb(uint16_t port, uint32_t value) {
	asm("out %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}
static inline uint32_t inb(uint16_t port) {
  uint32_t ret;
  //   display("aloha\n");
  asm("in %1, %0" : "=a"(ret) : "Nd"(port) : "memory" );
  //   printVal(ret);
  return ret;
}


static void display(const char *str){
	//prints string all at once
	outb(0xEB, (uintptr_t)str);
}

static void printVal(uint32_t val){
	outb(0xE9, val);
}

static uint32_t getNumExits(){
	return inb(0xEA);
}

static void printStr(char *p){
	// prints string character by character
	for (; *p; ++p)
		outb(0xE8, *p);
}

uint32_t openFile(const char *filePath, const char *mode){
	// port number F0
	typedef struct data{
		char filePath[100];
		char mode[3];
	}fileData;

	fileData d;
	int i;
	for(i=0; filePath[i]!=0; i++){
		d.filePath[i] = filePath[i];
	}
	d.filePath[i]=0;
	for(i=0; mode[i]!=0; i++){
		d.mode[i] = mode[i];
	}
	d.mode[i] = 0;
	outb(0xF0, (uint32_t)&d);
	uintptr_t filePointer = inb(0xF0);
	return filePointer;
	
}
static void writeFile(uint32_t fd, const char *str){
	// port number F1
	struct data{
		uint32_t filePointer;
		char str[100];
	}d;
	d.filePointer = fd;
	for(int i=0; str[i]!=0; i++){
		d.str[i] = str[i];
	}
	outb(0xF1, &d);
}

static void readFile(uint32_t fd, char *buffer, int nBytes){
	// port number F2
	struct data{
		uint32_t filePointer;
		// char str[100];
		uint32_t size;
	}d;
	d.filePointer = fd;
	d.size = nBytes;
	// d.str 
	outb(0xF2, &d);
	char *str = inb(0xF2);

	for(int i=0; i<=nBytes; i++){
		buffer[i] = str[i];
	}
	buffer[nBytes] = 0;
}

static int seekFile(uint32_t fd, int offset, int position){
	// port f3
	struct seekData{
		uint32_t filePointer;
		uint32_t offset;
		uint32_t position;
	}skData;
	skData.filePointer = fd;
	skData.offset = (uint32_t)offset;
	skData.position = (uint32_t) position;
	outb(0xf3, &skData);
	int status = (int)inb(0xf3);
	return status;
}
static void closeFile(uint32_t fd){
	// port number F4
	outb(0xF4, fd);
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	
	// printing a string one character at atime
	printStr("hello");

	// asking the host for number of exits
	uint32_t numExits = getNumExits();	
	printVal(numExits);
	
	// printing a 32-bit value
	uint32_t a = 1234;
	printVal(a);

	numExits = getNumExits();
	printVal(numExits);
	
	// printing a string, all at once
	char msg[] =  "Good Bye";
	display(msg);

	numExits = getNumExits();
	printVal(numExits);
	
	// opening a file
	uintptr_t fd1 = openFile("./abc.txt", "w");

	if(fd1 == -1){
		display("cannot open the file");
		return;
	}
	else{
		display("fd in guest: ");
		printVal(fd1);
	}
	// writing to a file
	writeFile(fd1, "Welcome to CS695, Assignment 1.");
	display("Done writing in file 1");
	// closing a file
	closeFile(fd1);

	
	fd1 = openFile("./abc.txt", "r");
	uintptr_t fd2 = openFile("xyz.txt", "w");

	// reading from a file
	char content[100];
	readFile(fd1, content, 10);
	display(content);


	// moving cursor in a file
	int status = seekFile(fd1, 2, SEEK_SET);
	if(status!=0){
		display("Error in seek");
	}
	// writing from one file to another file
	readFile(fd1, content, 10);
	display(content);
	writeFile(fd2, content);

	closeFile(fd1);
	closeFile(fd2);
	*(long *) 0x400 = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
