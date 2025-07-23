CC = riscv64-elf-gcc
OBJCOPY = riscv64-elf-objcopy
CFLAGS = -nostdlib -fno-builtin -mcmodel=medany -march=rv32im -mabi=ilp32 -ffreestanding

all: os.elf os.bin

os.elf: start.s os.c
	$(CC) $(CFLAGS) -T os.ld -o $@ $^ -lgcc

os.bin: os.elf
	$(OBJCOPY) -O binary $^ $@

clean:
	rm -f *.elf *.bin
