// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
//<<<<<<< HEAD
#include <kern/trap.h>

//=======
#include <kern/pmap.h>
>>>>>>> lab2
#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{"backtrace", "Display information about the trace", mon_backtrace},
	{"showmapping","Show the physical mappings", showmapping},
	{"set_memory", "set the permission bits", set_memory}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %KB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	struct Eipdebuginfo info;
	info.eip_file = "<unknown>";
	info.eip_line = 0;
	info.eip_fn_name = "<unknown>";
	info.eip_fn_namelen = 9;
	info.eip_fn_addr = 0;
	info.eip_fn_narg = 0;
        cprintf("Stack backtrace:\n");
	uint32_t ebp = read_ebp() ;
	uint32_t eip = *((uint32_t*) ebp + 1);
	while(ebp != 0){
		debuginfo_eip(eip , &info);
		cprintf("  ebp %08x  eip %08x  args ",ebp,eip);
		uint32_t* args = (uint32_t*)ebp + 2;
		for(int i = 0 ; i < 5 ; i++) {
			cprintf("%08x ",args[i]);
		}
		cprintf("\n");
		cprintf("         %s:%d: %.*s+%d\n" , info.eip_file, info.eip_line,info.eip_fn_namelen,info.eip_fn_name,(eip-info.eip_fn_addr));
		ebp = ((uint32_t*)ebp)[0];
		eip = ((uint32_t*)ebp)[1];
	}
	return 0;
}
void
pprint(pte_t *pte)
{
	cprintf("PTE_P: %x, PTE_W: %x, PTE_U: %x", *pte&PTE_P, *pte&PTE_W, *pte&PTE_U);
}
uint32_t
str_convert_addr(char *str)
{
	uint32_t ret = 0;
	str += 2;
	while(*str){
		if (*str > 'a')
			*str = *str - 'a' + '0' + 10;
		ret = ret * 16 + (*str -'0');
		str++;
	}
	return ret;
}
int
showmapping(int argc, char **argv, struct Trapframe *tf)
{
	if (argc <= 2){
		cprintf("this prompt need begin and end address\n");
		return -1;
	}
	uint32_t begin = str_convert_addr(argv[1]);
	uint32_t end   = str_convert_addr(argv[2]);
	cprintf("begin address: %x, end address: %x\n", begin, end);
	for(; begin <= end ; begin += PGSIZE){
		pte_t *pte = pgdir_walk(kern_pgdir, (void*)begin ,1);
		if(!pte) panic("this pte doesn't exist'");
		if(*pte & PTE_P){
			cprintf("pte:%x with:", begin);
			pprint(pte);
			cprintf("\n");
		}else
			cprintf("this pte can't be accessed");
	}
	return 0;
}
int
set_memory(int argc, char **argv, struct Trapframe *tf)
{
	int perm = 0;
	if(argc <= 3){
		cprintf("this prompt need other args\n");
		return -1;
	}
	uint32_t addr = str_convert_addr(argv[1]);
	pte_t *pte = pgdir_walk(kern_pgdir, (void*)addr, 1);
	if(!pte) panic("this pte doesn't exist");
	if(argv[2][0] == 'U')  perm = PTE_U ;
	if(argv[2][0] == 'P')  perm = PTE_P ;
	if(argv[2][0] == 'W')  perm = PTE_W ;
	if(argv[3][0] == '0')
		*pte = *pte & ~perm;
	else
		*pte = *pte & perm;
	cprintf("pte: %x PTE_%c changes to %c", addr, argv[2][0], argv[3][0]);
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
