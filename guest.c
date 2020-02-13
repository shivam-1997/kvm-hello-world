#include <stddef.h>
#include <stdint.h>
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

static void display(const char *str){
	//prints string all at once
	outb(0xEB, (uintptr_t)str);
}

static void printVal(uint32_t val){
	outb(0xE9, val);
}
static inline uint32_t inb(uint16_t port) {
  uint32_t ret;
//   display("aloha\n");
  asm("in %1, %0" : "=a"(ret) : "Nd"(port) : "memory" );
  printVal(ret);
  return ret;
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
	for(int i=0; filePath[i]!=0; i++){
		d.filePath[i] = filePath[i];
	}
	for(int i=0; mode[i]!=0; i++){
		d.mode[i] = mode[i];
	}
	
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
		int size;
	}d;
	d.filePointer = fd;
	d.size = nBytes;
	// d.str 
	outb(0xF2, &d);
	display("in k pehle\n");
	char *str = inb(0xF2);
	display("in k baad\n");
	
	display(str);
	// for(int i=0; str[i]!=0; i++){
	// 	buffer[i] = str[i];
	// }
	
}
static void closeFile(uint32_t fd){
	// port number F3
	outb(0xF3, fd);
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	
	printStr("hello world\n");
	
	uint32_t numExits = getNumExits();	
	printVal(numExits);

	uint32_t a = 1234;
	printVal(a);
	numExits = getNumExits();
	printVal(numExits);
	char msg[] =  "Good Bye\n";
	display(msg);
	numExits = getNumExits();
	printVal(numExits);
	
	uintptr_t fd = openFile("./abc.txt", "w");
	if(fd == -1){
		display("cannot open the file");
		return;
	}
	else{
		display("fd in guest = ");
		printVal(fd);
	}
	writeFile(fd, "Welcome to CS695, Assignment 1.\n");
	display("Done writing in file 1\n");
	
	closeFile(fd);
	uintptr_t fd2 = openFile("./abc.txt", "r");
	if(fd2 == -1){
		display("cannot open the file");
		return;
	}
	else{
		display("fd in guest = ");
		printVal(fd2);
	}
	char content[100];
	readFile(fd2, content, 100);
	// display(content);
	
	closeFile(fd2);

	*(long *) 0x400 = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
