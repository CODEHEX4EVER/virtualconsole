#include <stdint.h>

#define CLAMP(v, lo, hi) (v < lo) ? lo:(v > hi) ? hi:v

static uint32_t palette[12] = {
	0xFF80FF00,
	0xFF00FF00,
	0x0000FF00,
	0xADD8E600,
	0x00005000,
	0x00FFFF00,
	0xE0FFFF00,
	0x00006000,
	0xFFFF0000,
	0xFF000000,
	0xFF7F7F00,
	0x00FF0000
};

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

int computeMandelbrot(double re, double im, int iteration) {
    int i;
    double zR = re;
    double zI = im;

    for (i = 0; i < iteration; ++i) {
	    double r2 = zR * zR;
	    double i2 = zI * zI;
		
		if (r2 + i2 > 4.0) {
			return i;
		}
        
		zI = 2.0 * zR * zI + im;
		zR = r2 - i2 + re;
	}
    
	return iteration;
}

int os_main(void)
{
	int x,y,value;

	const double remin = -2.0;
	const double remax = 1.0;
	const double immin = -1.0;
	const double immax = 1.0;

	const double dx = (remax - remin)/(FRAMEBUFFER_X - 1);
	const double dy = (immax - immin)/(FRAMEBUFFER_Y - 1);

	lib_puts("Creating Mandelbrot set\n");
	uint32_t *framepointer_mem = (uint32_t *)FRAMEBUFFER_BASE;
	uint32_t *framepointer_vblank = (uint32_t *)FRAMEBUFFER_VBLANK;
	uint32_t *framepointer_swap = (uint32_t *)FRAMEBUFFER_SWAP;

//	int offset = 0;
	for(y = 0; y < FRAMEBUFFER_Y; y++){
		double im = immax -y * dy;

		for (x = 0; x < FRAMEBUFFER_X; x++){
			value = computeMandelbrot(remin + x * dx, im, 100);
			//offset++;
			if (value == 100)
				framepointer_mem[y * FRAMEBUFFER_X + x] = 0x0;
			else {
				value = CLAMP(value, 0, 11);
				framepointer_mem[y * FRAMEBUFFER_X  + x] = palette[value];
			}
			*framepointer_swap = 1;
			while(*framepointer_vblank){
			}
		}
	}
	lib_puts("Done!\n");
	//*framepointer_fram = 1;
	while (1) {};
	return 0;
}
