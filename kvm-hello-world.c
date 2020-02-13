/*shivam
	acts as a simple hypervisor,
	and runs the code within the file guest.c in a sandbox using the KVM API
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>

/* CR0 bits */
#define CR0_PE 1u
#define CR0_MP (1U << 1)
#define CR0_EM (1U << 2)
#define CR0_TS (1U << 3)
#define CR0_ET (1U << 4)
#define CR0_NE (1U << 5)
#define CR0_WP (1U << 16)
#define CR0_AM (1U << 18)
#define CR0_NW (1U << 29)
#define CR0_CD (1U << 30)
#define CR0_PG (1U << 31)

/* CR4 bits */
#define CR4_VME 1
#define CR4_PVI (1U << 1)
#define CR4_TSD (1U << 2)
#define CR4_DE (1U << 3)
#define CR4_PSE (1U << 4)
#define CR4_PAE (1U << 5)
#define CR4_MCE (1U << 6)
#define CR4_PGE (1U << 7)
#define CR4_PCE (1U << 8)
#define CR4_OSFXSR (1U << 8)
#define CR4_OSXMMEXCPT (1U << 10)
#define CR4_UMIP (1U << 11)
#define CR4_VMXE (1U << 13)
#define CR4_SMXE (1U << 14)
#define CR4_FSGSBASE (1U << 16)
#define CR4_PCIDE (1U << 17)
#define CR4_OSXSAVE (1U << 18)
#define CR4_SMEP (1U << 20)
#define CR4_SMAP (1U << 21)

#define EFER_SCE 1
#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)
#define EFER_NXE (1U << 11)

/* 32-bit page directory entry bits */
#define PDE32_PRESENT 1
#define PDE32_RW (1U << 1)
#define PDE32_USER (1U << 2)
#define PDE32_PS (1U << 7)

/* 64-bit page * entry bits */
#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_ACCESSED (1U << 5)
#define PDE64_DIRTY (1U << 6)
#define PDE64_PS (1U << 7)
#define PDE64_G (1U << 8)


struct vm {
	int sys_fd;
	int fd;
	char *mem;
};

void vm_init(struct vm *vm, size_t mem_size)
{
	int api_ver;
	struct kvm_userspace_memory_region memreg;

	vm->sys_fd = open("/dev/kvm", O_RDWR);
	if (vm->sys_fd < 0) {
		perror("open /dev/kvm");
		exit(1);
	}

	api_ver = ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0);
	if (api_ver < 0) {
		perror("KVM_GET_API_VERSION");
		exit(1);
	}

	if (api_ver != KVM_API_VERSION) {
		fprintf(stderr, "Got KVM api version %d, expected %d\n",
			api_ver, KVM_API_VERSION);
		exit(1);
	}

	vm->fd = ioctl(vm->sys_fd, KVM_CREATE_VM, 0);
	if (vm->fd < 0) {
		perror("KVM_CREATE_VM");
		exit(1);
	}

    if (ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000) < 0) {
        perror("KVM_SET_TSS_ADDR");
		exit(1);
	}
	// shivam
	// allocating memory to the guest os
	vm->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		   			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
					 -1, 0);
	if (vm->mem == MAP_FAILED) {
		perror("mmap mem");
		exit(1);
	}
	// shivam
	printf("The memory allocated for the guest OS is %lu Bytes.\n", mem_size);
	
	madvise(vm->mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)vm->mem;
    if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
                exit(1);
	}
	printf("The guest OS is starting at %p.\n", vm->mem);
}

struct vcpu {
	int fd;
	struct kvm_run *kvm_run;
};

void vcpu_init(struct vm *vm, struct vcpu *vcpu)
{
	int vcpu_mmap_size;

	vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, 0);
        if (vcpu->fd < 0) {
		perror("KVM_CREATE_VCPU");
                exit(1);
	}

	vcpu_mmap_size = ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (vcpu_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
                exit(1);
	}
	// shivam
	// allocating memory to vCPU,
	// to store the information it has to exchange with KVM
	vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, vcpu->fd, 0);
	if (vcpu->kvm_run == MAP_FAILED) {
		perror("mmap kvm_run");
		exit(1);
	}
	// shivam
	printf("The memory allocated for vCPU is %d Bytes\n.", vcpu_mmap_size);
	printf("The vCPU memory is starting at %p.\n", vcpu->kvm_run);
}

#define MAX_OPEN_FILES 150	// by shivam, tells the max size of file descriptor table
uint32_t findEmptyIndex(FILE *fileDescArray[]){
	for(int i=0; i<MAX_OPEN_FILES; i++){
		if(fileDescArray[i] == NULL)
			return i;
	}
	return -1;
}
int run_vm(struct vm *vm, struct vcpu *vcpu, size_t sz)
{
	struct kvm_regs regs;
	uint64_t memval = 0;
	// added by shivam
	uint32_t numExits=0;
	char filePath[100];
	char mode[3];
	uint32_t fileDesc_index;
	FILE *filePointer;
	FILE *fileDescArray[MAX_OPEN_FILES];
	for(int i=0; i<MAX_OPEN_FILES; i++){
		fileDescArray[i] = NULL;
	}
	uint32_t size;

	for (;;) {
		// start running the guest
		if (ioctl(vcpu->fd, KVM_RUN, 0) < 0) {
			perror("KVM_RUN");
			exit(1);
		}
		numExits++;
		printf("EXIT->");
		// in next line the control switches back to hypervisor from guest
		switch (vcpu->kvm_run->exit_reason) {
		case KVM_EXIT_HLT:
			goto check;

		case KVM_EXIT_IO:
			// printing 8-bit data
			if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT
			    && vcpu->kvm_run->io.port == 0xE8) {
				const char *p = (char *)vcpu->kvm_run;
				const uint8_t *n = (p + vcpu->kvm_run->io.data_offset);
				fwrite( n, vcpu->kvm_run->io.size, 1, stdout);
				fflush(stdout);
				continue;
			}
			// prints 32-bit unsigned data
			if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT
			    && vcpu->kvm_run->io.port == 0xE9) {
				const char *p = (char *)vcpu->kvm_run;
				const uint32_t *n = (p + vcpu->kvm_run->io.data_offset);

				fflush(stdout);
				// fwrite( k, vcpu->kvm_run->io.size, s, stdout);
				// fwrite( k, sizeof(*k), 1, stdout);
				fprintf(stdout, "%lu\n", *n);
				fflush(stdout);
				continue;
			}
			// returns number of exits till now
			if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN
			    && vcpu->kvm_run->io.port == 0xEA) {
				
				// printf("in %d\n", numExits);

				uint8_t *p = (uint8_t *)vcpu->kvm_run;

				// fwrite(&numExits, sizeof(uint32_t), 1, p + vcpu->kvm_run->io.data_offset);
				memcpy(p + vcpu->kvm_run->io.data_offset, &numExits, sizeof(numExits));
				// fprintf( p + vcpu->kvm_run->io.data_offset , "%d", numExits);
				// fflush(stdin);
				continue;
			}

			// prints entire string at once
			if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT
			    && vcpu->kvm_run->io.port == 0xEB) {
				
				uint8_t *vcpu_kvm =  (uint8_t *)vcpu->kvm_run;
				uint32_t  *data_guest_virt = (uint32_t *)(vcpu_kvm + vcpu->kvm_run->io.data_offset);
				char *str = vm->mem + *data_guest_virt  ;
	
				
				fflush(stdout);
				fprintf(stdout, "%s", str);
				// fprintf(stdout, "%s\n", &vm->mem[ vcpu_kvm[vcpu->kvm_run->io.data_offset]]);
		
				fflush(stdout);
				continue;
			}

			//opens a file			
			if(vcpu->kvm_run->io.port == 0xF0){
				typedef struct data{
					char filePath[100];
					char mode[3];
				}fileData;
				// reads file path and mode
				if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT) {

					uint8_t *vcpu_kvm =  (uint8_t *)vcpu->kvm_run;
					uint32_t  *data_guest_virt = (uint32_t *)(vcpu_kvm + vcpu->kvm_run->io.data_offset);
					fileData *fdt = vm->mem + *data_guest_virt  ;
	
					strcpy(filePath, fdt->filePath);
					strcpy(mode, fdt->mode);
					
					continue;
				}
				// opens fle 
				if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN) {

					FILE *filePointer = fopen(filePath, mode);
					uint32_t emptyIndex = findEmptyIndex(fileDescArray);
					fileDescArray[emptyIndex] = filePointer;
					
					uint8_t *p = (uint8_t *)vcpu->kvm_run;
					// memcpy(p + vcpu->kvm_run->io.data_offset, fp, 4);
					memcpy(p + vcpu->kvm_run->io.data_offset, &emptyIndex, sizeof(emptyIndex));
					
					continue;
				}
				
			}
			//writes in a file
			if(vcpu->kvm_run->io.port == 0xF1 && vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT){
				
				typedef struct data{
					uint32_t filePointer;
					char str[100];
				}wData;
				
				uint8_t *vcpu_kvm =  (uint8_t *)vcpu->kvm_run;
				uint32_t  *data_guest_virt = (uint32_t *)(vcpu_kvm + vcpu->kvm_run->io.data_offset);
				wData *wdt = vm->mem + *data_guest_virt  ;
				
				// fprintf(stdout, "filePath = %s\n", fdt->filePath);
				uint32_t fileDesc_index = wdt->filePointer;
				// fprintf(stdout, "file pointer received in host = %lu \n", filePointer);
				// fprintf(stdout, "string to be written = %s\n", wdt->str);
				// fflush(stdout);
				
				// fprintf(filePointer, "%s", wdt->str);
				// fflush(filePointer);
					
				if(fprintf(fileDescArray[(int)(fileDesc_index)], "%s", wdt->str)>0){
					printf("Successfully written to the file\n");
				}
				fflush(fileDescArray[ (int)(fileDesc_index)]);
				
				continue;
			}
			//reads a file
			if(vcpu->kvm_run->io.port == 0xF2 ){
				printf("**********reading from a file\n");
				fflush(stdout);
				
				typedef struct data{
					uint32_t filePointer;
					// char str[100];
					int size;
				}rData;
				
				if( vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT){

					printf("copying file descriptor\n");
					fflush(stdout);
				
					uint8_t *vcpu_kvm =  (uint8_t *)vcpu->kvm_run;
					uint32_t  *data_guest_virt = (uint32_t *)(vcpu_kvm + vcpu->kvm_run->io.data_offset);
					rData *rdt = vm->mem + *data_guest_virt  ;
					
					uint32_t fileDesc_index = rdt->filePointer;
					
					filePointer = fileDescArray[fileDesc_index];
					size = rdt -> size;
					printf("fd in host = %lu\n", fileDesc_index);
					fflush(stdout);
					
					continue;
				}
				if( vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN){
					printf("reading to buffer \n");
					fflush(stdout);
				
					char *buffer =  (char*)malloc(size);
					
					// size_t fread ( void * ptr, size_t size, size_t count, FILE * stream );
					fread(buffer, sizeof(char), size, filePointer);
					printf("content in file: %s\n", buffer);
					fflush(stdout);
					uint8_t *p = (uint8_t *)vcpu->kvm_run;
					memcpy(p + vcpu->kvm_run->io.data_offset, &buffer, sizeof(buffer));
					continue;
				}
				
			}
			//closes a file
			if(vcpu->kvm_run->io.port == 0xF3 &&
				vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT){
				
				const char *p = (char *)vcpu->kvm_run;
				const uint32_t *index_loc = (p + vcpu->kvm_run->io.data_offset);
				uint32_t index = *index_loc;

				if( fclose(fileDescArray[index]) == 0){
					fileDescArray[index] = NULL;
					fprintf(stdout, "file closed successfully\n");
				}
				else{
					fprintf(stdout, "Error in closing file\n");
				}
				fflush(stdout);
				continue;
			}

			/* fall through */
		default:
			fprintf(stderr,	"Got exit_reason %d,"
				" expected KVM_EXIT_HLT (%d)\n",
				vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
			exit(1);
		}
	}

 check:
	if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		exit(1);
	}

	if (regs.rax != 42) {
		printf("Wrong result: {E,R,}AX is %lld\n", regs.rax);
		return 0;
	}

	memcpy(&memval, &vm->mem[0x400], sz);
	if (memval != 42) {
		printf("Wrong result: memory at 0x400 is %lld\n",
		       (unsigned long long)memval);
		return 0;
	}

	return 1;
}

extern const unsigned char guest16[], guest16_end[];

int run_real_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing real mode\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	sregs.cs.selector = 0;
	sregs.cs.base = 0;

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, guest16, guest16_end-guest16);
	return run_vm(vm, vcpu, 2);
}

static void setup_protected_mode(struct kvm_sregs *sregs)
{
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 11, /* Code: execute, read, accessed */
		.dpl = 0,
		.db = 1,
		.s = 1, /* Code/data */
		.l = 0,
		.g = 1, /* 4KB granularity */
	};

	sregs->cr0 |= CR0_PE; /* enter protected mode */

	sregs->cs = seg;

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2 << 3;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = seg;
}

extern const unsigned char guest32[], guest32_end[];

int run_protected_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing protected mode\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(&sregs);

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, guest32, guest32_end-guest32);
	return run_vm(vm, vcpu, 4);
}

static void setup_paged_32bit_mode(struct vm *vm, struct kvm_sregs *sregs)
{
	uint32_t pd_addr = 0x2000;
	uint32_t *pd = (void *)(vm->mem + pd_addr);

	/* A single 4MB page to cover the memory region */
	pd[0] = PDE32_PRESENT | PDE32_RW | PDE32_USER | PDE32_PS;
	/* Other PDEs are left zeroed, meaning not present. */

	sregs->cr3 = pd_addr;
	sregs->cr4 = CR4_PSE;
	sregs->cr0
		= CR0_PE | CR0_MP | CR0_ET | CR0_NE | CR0_WP | CR0_AM | CR0_PG;
	sregs->efer = 0;
}

int run_paged_32bit_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing 32-bit paging\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(&sregs);
	setup_paged_32bit_mode(vm, &sregs);

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, guest32, guest32_end-guest32);
	return run_vm(vm, vcpu, 4);
}
// shivam
// long(64-bit mode) fucntions start
extern const unsigned char guest64[], guest64_end[];

// this function sets segment registers
static void setup_64bit_code_segment(struct kvm_sregs *sregs)
{
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 11, /* Code: execute, read, accessed */
		.dpl = 0,	/* privelege level 0 */
		.db = 0,
		.s = 1, 	/* Code/data */
		.l = 1,
		.g = 1, 	/* 4KB granularity */
	};

	//shivam
	// code segment
	sregs->cs = seg; 

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2 << 3;
	// data segment = es = fs = gs = tack segment
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = seg;
}

// this function sets page table and 
// then calls setup_64bit_code_segment to set registers
static void setup_long_mode(struct vm *vm, struct kvm_sregs *sregs)
{
	// pml4t -> pdpt -> pdt -> pt -> physical address
	uint64_t pml4_addr = 0x2000;
	uint64_t *pml4 = (void *)(vm->mem + pml4_addr);

	uint64_t pdpt_addr = 0x3000;
	uint64_t *pdpt = (void *)(vm->mem + pdpt_addr);

	uint64_t pd_addr = 0x4000;
	uint64_t *pd = (void *)(vm->mem + pd_addr);

	// PDE64_PRESENT - page is mapped
	// PDE64_RW		 - page is writeable
	// PDE64_USER	 - page can be accessed in user-mode
	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;
	// PDE64_PS -  stands for it's 2M paging instead of 4K. 
	// As a result, these page tables can map 
	// address below 0x200000 to itself 
	// (i.e. virtual address equals to physical address).
	pd[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;

	sregs->cr3 = pml4_addr; // cr3 should point to physical address of pml4
	sregs->cr4 = CR4_PAE;	// 1<<5
	sregs->cr0 = CR0_PE | CR0_MP | CR0_ET | CR0_NE | CR0_WP | CR0_AM | CR0_PG; // 0x80050033
	sregs->efer = EFER_LME | EFER_LMA; // 0x500

	// now we will set segment registers
	setup_64bit_code_segment(sregs);
}

int run_long_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing 64-bit mode\n");

    if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_long_mode(vm, &sregs);

    if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;
	/* Create stack at top of 2 MB page and grow down. */
	regs.rsp = 2 << 20;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}
	// shivam
	// copying the guest code into the memory allocated by host
	memcpy(vm->mem, guest64, guest64_end-guest64);
	// run_vm will start running the guest OS
	return run_vm(vm, vcpu, 8);
}

// shivam
// long(64-bit mode) functions end

int main(int argc, char **argv)
{
	struct vm vm;
	struct vcpu vcpu;
	enum {
		REAL_MODE,
		PROTECTED_MODE,
		PAGED_32BIT_MODE,
		LONG_MODE,
	} mode = REAL_MODE;
	int opt;

	// shivam
	// finding the mode to run in using command line argument
	while ((opt = getopt(argc, argv, "rspl")) != -1) {
		switch (opt) {
		case 'r':
			mode = REAL_MODE;
			break;

		case 's':
			mode = PROTECTED_MODE;
			break;

		case 'p':
			mode = PAGED_32BIT_MODE;
			break;

		case 'l':
			mode = LONG_MODE;
			break;

		default:
			fprintf(stderr, "Usage: %s [ -r | -s | -p | -l ]\n",
				argv[0]);
			return 1;
		}
	}

	// shivam
	// The code within these two functions
	// allocates memory for the guest and its VCPU respectively
	vm_init(&vm, 0x200000);
	vcpu_init(&vm, &vcpu);

	// shivam
	// now the program proceeds to format the guest memory area and CPU registers
	// by configuring these values in KVM API
	switch (mode) {
	case REAL_MODE:
		return !run_real_mode(&vm, &vcpu);

	case PROTECTED_MODE:
		return !run_protected_mode(&vm, &vcpu);

	case PAGED_32BIT_MODE:
		return !run_paged_32bit_mode(&vm, &vcpu);

	case LONG_MODE:
		return !run_long_mode(&vm, &vcpu);
	}

	return 1;
}
