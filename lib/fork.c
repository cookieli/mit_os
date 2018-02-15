// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if(!((err & FEC_WR) && (uvpd[PDX(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_P)&& (uvpt[PGNUM(addr)]&PTE_COW)))
		panic("can't write on copy");
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);
	if (sys_page_alloc(0, (void *)PFTEMP, PTE_W|PTE_U|PTE_P) < 0)
		panic("can't alloc page");
	memcpy((void *)PFTEMP, addr, PGSIZE);
	if(sys_page_map(0, PFTEMP, 0, addr, PTE_W|PTE_U|PTE_P) < 0)
		panic("sys_page_map");
	if(sys_page_unmap(0, PFTEMP) < 0)
		panic("sys_page_unmap.");

//	panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	void *addr = (void *)(pn * PGSIZE);
	if(uvpt[pn]&PTE_W || uvpt[pn]&PTE_COW){
		if(sys_page_map(0, addr, envid, addr, PTE_P|PTE_U|PTE_COW) < 0)
			panic("2");
		if(sys_page_map(0, addr, 0, addr, PTE_P|PTE_U|PTE_COW) < 0)
			panic("3");
	}else  if(sys_page_map(0, addr, envid, addr, PTE_P|PTE_U) < 0)
		panic("4");
//	panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
//Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	envid_t envid; 
	uint32_t addr;
	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	for(addr = 0; addr < USTACKTOP; addr += PGSIZE){
		if((uvpd[PDX(addr)] & PTE_P )
		   && (uvpt[PGNUM(addr)] & PTE_P)
		   && (uvpt[PGNUM(addr)] & PTE_U))
			duppage(envid, PGNUM(addr));
	}
	if (sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W|PTE_P|PTE_U) < 0)
		panic("1");
	extern void _pgfault_upcall(void);
//	void(*_pgfault_handler)(struct UTrapframe *utf);
	if(sys_env_set_pgfault_upcall(envid, (void *)_pgfault_upcall) < 0)
		panic("upcall");
//	_pgfault_handler = pgfault;
	if(sys_env_set_status(envid, ENV_RUNNABLE) < 0)
		panic("5");
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
