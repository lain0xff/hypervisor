gcc  = g++ -m64 -fpermissive -fno-exceptions -fno-rtti -nostdlib -ffreestanding
nasm = nasm -f elf64
ld   = ld -m elf_x86_64

srcdir = src

all: kernel iso clean

clean:
	rm -f *.o
	rm -rf ./obj

kernel:
	mkdir -p obj
	$(nasm) $(srcdir)/bootloader.asm   -o ./obj/bootloader.o
	$(nasm) $(srcdir)/kernelloader.asm -o ./obj/kernelloader.o
	$(gcc) -c $(srcdir)/kernel.cpp     -o ./obj/kernel.o
	$(gcc) -c $(srcdir)/idt.cpp        -o ./obj/idt.o
	$(gcc) -c $(srcdir)/vmx.cpp        -o ./obj/vmx.o
	$(gcc) -c $(srcdir)/ept.cpp        -o ./obj/ept.o

	$(ld) -T ./linker.ld -o ./obj/kernel.bin \
		./obj/bootloader.o \
		./obj/kernelloader.o \
		./obj/kernel.o \
		./obj/idt.o \
		./obj/vmx.o \
		./obj/ept.o

iso:
	mkdir -p Hypervisor/boot
	mv ./obj/kernel.bin Hypervisor/boot/kernel.bin
	grub-mkrescue -o Hypervisor.iso Hypervisor