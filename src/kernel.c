// ----- Pre-processor constants -----
#define ROWS 25
#define COLS 80
// IDT_SIZE: Specific to x86 architecture
#define IDT_SIZE 256
// KERNEL_CODE_SEGMENT_OFFSET: the first segment after the null segment in gdt.asm
#define KERNEL_CODE_SEGMENT_OFFSET 0x8
// 32-bit Interrupt gate: 0x8E
// ( P=1, DPL=00b, S=0, type=1110b => type_attr=1000_1110b=0x8E) (thanks osdev.org)
#define IDT_INTERRUPT_GATE_32BIT 0x8e
// IO Ports for PICs
#define PIC1_COMMAND_PORT 0x20
#define PIC1_DATA_PORT 0x21
#define PIC2_COMMAND_PORT 0xA0
#define PIC2_DATA_PORT 0xA1
// IO Ports for Keyboard
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// ----- Includes -----
#include "keyboard_map.h"

// ----- External functions -----
extern void print_char_with_asm(char c, int row, int col);
extern void load_gdt();
extern void keyboard_handler();
extern char ioport_in(unsigned short port);
extern void ioport_out(unsigned short port, unsigned char data);
extern void load_idt(unsigned int* idt_address);
extern void enable_interrupts();

// ----- Structs -----
struct IDT_pointer {
	unsigned short limit;
	unsigned int base;
} __attribute__((packed));
struct IDT_entry {
	unsigned short offset_lowerbits; // 16 bits
	unsigned short selector; // 16 bits
	unsigned char zero; // 8 bits
	unsigned char type_attr; // 8 bits
	unsigned short offset_upperbits; // 16 bits
} __attribute__((packed));

// ----- Global variables -----
struct IDT_entry IDT[IDT_SIZE]; // This is our entire IDT. Room for 256 interrupts
int cursor_pos = 0;

void init_idt() {
	// Get the address of the keyboard_handler code in kernel.asm as a number
	unsigned int offset = (unsigned int)keyboard_handler;
	// Populate the first entry of the IDT
	// TODO why 0x21 and not 0x0?
	// My guess: 0x0 to 0x2 are reserved for CPU, so we use the first avail
	IDT[0x21].offset_lowerbits = offset & 0x0000FFFF; // lower 16 bits
	IDT[0x21].selector = KERNEL_CODE_SEGMENT_OFFSET;
	IDT[0x21].zero = 0;
	IDT[0x21].type_attr = IDT_INTERRUPT_GATE_32BIT;
	IDT[0x21].offset_upperbits = (offset & 0xFFFF0000) >> 16;
	// Program the PICs - Programmable Interrupt Controllers
	// Background:
		// In modern architectures, the PIC is not a separate chip.
		// It is emulated in the CPU for backwards compatability.
		// The APIC (Advanced Programmable Interrupt Controller)
		// is the new version of the PIC that is integrated into the CPU.
		// Default vector offset for PIC is 8
		// This maps IRQ0 to interrupt 8, IRQ1 to interrupt 9, etc.
		// This is a problem. The CPU reserves the first 32 interrupts for
		// CPU exceptions such as divide by 0, etc.
		// In programming the PICs, we move this offset to 0x2 (32) so that
		// we can handle all interrupts coming to the PICs without overlapping
		// with any CPU exceptions.

	// Send ICWs - Initialization Command Words
	// PIC1: IO Port 0x20 (command), 0xA0 (data)
	// PIC2: IO Port 0x21 (command), 0xA1 (data)
	// ICW1: Initialization command
	// Send a fixed value of 0x11 to each PIC to tell it to expect ICW2-4
	// Restart PIC1
	ioport_out(PIC1_COMMAND_PORT, 0x11);
	ioport_out(PIC2_COMMAND_PORT, 0x11);
	// ICW2: Vector Offset (this is what we are fixing)
	// Start PIC1 at 32 (0x20 in hex) (IRQ0=0x20, ..., IRQ7=0x27)
	// Start PIC2 right after, at 40 (0x28 in hex)
	ioport_out(PIC1_DATA_PORT, 0x20);
	ioport_out(PIC2_DATA_PORT, 0x28);
	// ICW3: Cascading (how master/slave PICs are wired/daisy chained)
	// Tell PIC1 there is a slave PIC at IRQ2 (why 4? don't ask me - https://wiki.osdev.org/8259_PIC)
	// Tell PIC2 "its cascade identity" - again, I'm shaky on this concept. More resources in notes
	ioport_out(PIC1_DATA_PORT, 0x0);
	ioport_out(PIC2_DATA_PORT, 0x0);
	// ICW4: "Gives additional information about the environemnt"
	// See notes for some potential values
	// We are using 8086/8088 (MCS-80/85) mode
	// Not sure if that's relevant, but there it is.
	// Other modes appear to be special slave/master configurations (see wiki)
	ioport_out(PIC1_DATA_PORT, 0x1);
	ioport_out(PIC2_DATA_PORT, 0x1);
	// Voila! PICs are initialized

	// Mask all interrupts (why? not entirely sure)
	// 0xff is 16 bits that are all 1.
	// This masks each of the 16 interrupts for that PIC.
	ioport_out(PIC1_DATA_PORT, 0xff);
	ioport_out(PIC2_DATA_PORT, 0xff);

	// Last but not least, we fill out the IDT descriptor
	// and load it into the IDTR register of the CPU,
	// which is all we need to do to make it active.
	struct IDT_pointer idt_ptr;
	idt_ptr.limit = (sizeof(struct IDT_entry) * IDT_SIZE) - 1;
	idt_ptr.base = (unsigned int) &IDT;
	// Now load this IDT
	load_idt(&idt_ptr);
}

void kb_init() {
	// 0xFD = 1111 1101 in binary. enables only IRQ1
	// Why IRQ1? Remember, IRQ0 exists, it's 0-based
	ioport_out(PIC1_DATA_PORT, 0xFD);
}

void handle_keyboard_interrupt() {
	// Write end of interrupt (EOI)
	ioport_out(PIC1_COMMAND_PORT, 0x20);

	unsigned char status = ioport_in(KEYBOARD_STATUS_PORT);
	// Lowest bit of status will be set if buffer not empty
	// (thanks mkeykernel)
	if (status & 0x1) {
		char keycode = ioport_in(KEYBOARD_DATA_PORT);
		if (keycode < 0 || keycode >= 128) return; // how did they know keycode is signed?
		print_char_with_asm(keyboard_map[keycode],0,cursor_pos);
		cursor_pos++;
	}
}

void print_message() {
	// Fill the screen with 'x'
	int i, j;
	for (i = 0; i < COLS; i++) {
		for (j = 0; j < ROWS; j++) {
			print_char_with_asm('*',j,i);
		}
	}
	print_char_with_asm('-',0,0);
	print_char_with_asm('P',0,1);
	print_char_with_asm('K',0,2);
	print_char_with_asm('O',0,3);
	print_char_with_asm('S',0,4);
	print_char_with_asm('-',0,5);
}

// ----- Entry point -----
void main() {
	print_message();
	// load_gdt();
	init_idt();
	kb_init();
	enable_interrupts();
	// Finish main execution, but don't halt the CPU. Same as `jmp $` in assembly
	while(1);
}
