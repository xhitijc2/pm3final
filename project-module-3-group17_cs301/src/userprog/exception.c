#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

void exception_init(void)
{
    intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
    intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
    intr_register_int(5, 3, INTR_ON, kill,
                      "#BR BOUND Range Exceeded Exception");

    intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
    intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
    intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
    intr_register_int(7, 0, INTR_ON, kill,
                      "#NM Device Not Available Exception");
    intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
    intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
    intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
    intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
    intr_register_int(19, 0, INTR_ON, kill,
                      "#XF SIMD Floating-Point Exception");

    intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

void exception_print_stats(void)
{
    printf("Exception: %lld page faults\n", page_fault_cnt);
}

static void
kill(struct intr_frame *frme)
{
    switch (frme->cs)
    {
    case SEL_UCSEG:
        printf("%s: dying due to interrupt %#04x (%s).\n",
               thread_name(), frme->vec_no, intr_name(frme->vec_no));
        intr_dump_frame(frme);
        thread_exit_with_status(-1);

    case SEL_KCSEG:
        intr_dump_frame(frme);
        PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
        /* Some other code segment?  Shouldn't happen.  Panic the
           kernel. */
        printf("Interrupt %#04x (%s) in unknown segment %04x\n",
               frme->vec_no, intr_name(frme->vec_no), frme->cs);
        thread_exit_with_status(-1);
    }
}

static void
page_fault(struct intr_frame *frme)
{
    bool pageNotPresent; /* True: not-present page, false: writing r/o page. */
    bool write;          /* True: access was write, false: access was read. */
    bool user;           /* True: access by user, false: access by kernel. */
    void *faultAddress;  /* Fault address. */

    asm("movl %%cr2, %0"
        : "=r"(faultAddress));

    intr_enable();

    page_fault_cnt++;

    pageNotPresent = (frme->error_code & PF_P) == 0;
    write = (frme->error_code & PF_W) != 0;
    user = (frme->error_code & PF_U) != 0;

    struct thread *current = thread_current();
    bool is_valid_fault = pageNotPresent && faultAddress != NULL && is_user_vaddr(faultAddress);
    if (is_valid_fault)
    {
        bool handled = pt_suppl_handle_page_fault(faultAddress, frme);

        if (handled)
        {
            bool success = pagedir_get_page(current->pagedir, faultAddress);
            if (!success)
                thread_exit_with_status(-1);
            else
                return;
        }
        else
            thread_exit_with_status(-1);
    }
    printf("Page fault at %p: %s error %s page in %s context.\n",
           faultAddress,
           pageNotPresent ? "not present" : "rights violation",
           write ? "writing" : "reading",
           user ? "user" : "kernel");
    kill(frme);
}

