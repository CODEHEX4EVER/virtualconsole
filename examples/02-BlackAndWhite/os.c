#include <stdint.h>

#define UART        0x10000000
#define UART_THR    (uint8_t*)(UART+0x00) // THR:transmitter holding register
#define UART_LSR    (uint8_t*)(UART+0x05) // LSR:line status register
#define UART_LSR_EMPTY_MASK 0x40          // LSR Bit 6: Transmitter empty; both the THR and LSR are empty

#define FRAMEBUFFER_VBLANK	0x10038000
#define FRAMEBUFFER_SWAP	0x10038004
#define FRAMEBUFFER_BASE  	0x10000100
#define FRAMEBUFFER_SIZE32	57344
#define FRAMEBUFFER_X		256
#define FRAMEBUFFER_Y		224

int lib_putc(char ch) {
	while ((*UART_LSR & UART_LSR_EMPTY_MASK) == 0);
	return *UART_THR = ch;
}

void lib_puts(char *s) {
	while (*s) lib_putc(*s++);
}

int os_main(void)
{
	lib_puts("Black And White framebuffer :D\n");
	uint32_t *framepointer_mem = (uint32_t *)FRAMEBUFFER_BASE;
	uint32_t *framepointer_vblank = (uint32_t *)FRAMEBUFFER_VBLANK;	
	uint32_t *framepointer_swap = (uint32_t *)FRAMEBUFFER_SWAP;
	uint32_t color = 0x00000000;
	int y, x;
	while(1) {
		for(y = 0; y < FRAMEBUFFER_Y; y++){
			for (x = 0; x < FRAMEBUFFER_X; x++){
				framepointer_mem[y * FRAMEBUFFER_X + x] = color;
			}
		}
		*framepointer_swap = 1;
		y, x = 0;
		color += 0x0F0F0F00;
		while(!*framepointer_vblank){ // wait vblank
		}
		if (color == 0xFFFFFF00)
			color = 0x00000000;
	}
	lib_puts("Done!\n");
	//*framepointer_fram = 1;
	while (1) {};
	return 0;
}
