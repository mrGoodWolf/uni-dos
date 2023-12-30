BOOT=src/kernel.asm
KERNEL=src/kernel.c
ISOFILE=build/DOSUNIX.iso
ISO_VOLUME_NAME=XDOS
LINKER=src/linker.ld
KERNEL_OUT=build/xdos.bin
ISO_OUT=build/unidos.iso

all: build
build: clean	mkdir -p build
	nasm -f elf32 ${BOOT} -o build/boot.o
	gcc -m32 -ffreestanding -c ${KERNEL} -o build/kernel.o
	ld -m elf_i386 -T ${LINKER} -o ${KERNEL_OUT} build/boot.o build/kernel.o
build_debug: clean
	mkdir -p build
	nasm -f elf32 ${BOOT} -o build/boot.o
	gcc -m32 -ffreestanding -c ${KERNEL} -o build/kernel.o -ggdb
	ld -m elf_i386 -T ${LINKER} -o ${KERNEL_OUT} build/boot.o build/kernel.o
run: build
	qemu-system-i386 -kernel ${KERNEL_OUT} -monitor stdio
run-iso: iso 
	qemu-system-i386 -cdrom ${ISO_OUT} -monitor stdio
debug: build_debug
	qemu-system-i386 -kernel ${KERNEL_OUT} -s -S &
	gdb -x .gdbinit
iso: build
	mkdir -p build/iso/boot/grub
	cp grub.cfg build/iso/boot/grub
	cp ${KERNEL_OUT} build/iso/boot/grub
	grub-mkrescue -o ${ISO_OUT} build/iso
	rm -rf build/iso
run-iso: iso
	qemu-system-i386 -cdrom ${ISOFILE}
clean:
	rm -rf build
                          