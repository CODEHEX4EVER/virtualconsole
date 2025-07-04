// Copyright 2022 Charles Lohr, you may use this file or any portions herein under any of the BSD, MIT, or CC0 licenses.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

// mmio buffer
uint32_t mmio_size = 0x02000000;
uint8_t *mmio_image = 0;

// SDL2
#include <SDL2/SDL.h>
#define FRAMEBUFFER_BASE 0x10000100
#define FRAMEBUFFER_X 256
#define FRAMEBUFFER_Y 224
#define FRAMEBUFFER_DEPTH 4
#define FRAMEBUFFER_SIZE32 (FRAMEBUFFER_X * FRAMEBUFFER_Y)
#define FRAMEBUFFER_SIZE8 (FRAMEBUFFER_X * FRAMEBUFFER_Y * FRAMEBUFFER_DEPTH) // 256x224 framebuffer with 4 depth bytes (RGBA)
#define FRAMEBUFFER_HZ 60
const uint64_t framebuffer_interval = 1000000ULL / FRAMEBUFFER_HZ;
SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
SDL_Event event;
uint32_t *framebuffer_addr;
uint32_t *framebuffer_buffer;

// The virtual console has 8MB of ram.
uint32_t ram_amt = 8*1024*1024;
int fail_on_all_faults = 0;

// cpu speed
#define LIMITED_CPU 0
#define TARGET_HZ 50000000;

static uint64_t GetTimeMicroseconds();
static void ResetKeyboardInput();
static void CaptureKeyboardInput();
static uint32_t HandleException( uint32_t ir, uint32_t retval );
static uint32_t HandleControlStore( uint32_t addy, uint32_t val );
static uint32_t HandleControlLoad( uint32_t addy );
static void HandleOtherCSRWrite( uint8_t * image, uint16_t csrno, uint32_t value );
static int32_t HandleOtherCSRRead( uint8_t * image, uint16_t csrno );
static void MiniSleep();
static int IsKBHit();
static int ReadKBByte();

// This is the functionality we want to override in the emulator.
//  think of this as the way the emulator's processor is connected to the outside world.
#define MINIRV32WARN( x... ) printf( x );
#define MINIRV32_DECORATE  static
#define MINI_RV32_RAM_SIZE ram_amt
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_POSTEXEC( pc, ir, retval ) { if( retval > 0 ) { if( fail_on_all_faults ) { printf( "FAULT\n" ); return 3; } else retval = HandleException( ir, retval ); } }
#define MINIRV32_HANDLE_MEM_STORE_CONTROL( addy, val ) if( HandleControlStore( addy, val ) ) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL( addy, rval ) rval = HandleControlLoad( addy );
#define MINIRV32_OTHERCSR_WRITE( csrno, value ) HandleOtherCSRWrite( image, csrno, value );
#define MINIRV32_OTHERCSR_READ( csrno, value ) value = HandleOtherCSRRead( image, csrno );

#include "mini-rv32ima.h"

uint8_t * ram_image = 0;
struct MiniRV32IMAState * core;
const char * kernel_command_line = 0;

static void DumpState( struct MiniRV32IMAState * core, uint8_t * ram_image );

int main( int argc, char ** argv )
{
	int i;
	long long instct = -1;
	int show_help = 0;
	int time_divisor = 1;
	int fixed_update = 0;
	int do_sleep = 1;
	const char * bios_file_name = 0;
	for( i = 1; i < argc; i++ )
	{
		const char * param = argv[i];
		int param_continue = 0; // Can combine parameters, like -lpt x
		do
		{
			if( param[0] == '-' || param_continue )
			{
				switch( param[1] )
				{
				case 'b': bios_file_name = (++i<argc)?argv[i]:0; break;
				default:
					if( param_continue )
						param_continue = 0;
					else
						show_help = 1;
					break;
				}
			}
			else
			{
				show_help = 1;
				break;
			}
			param++;
		} while( param_continue );
	}
	if( show_help || bios_file_name == 0 )
	{
		fprintf( stderr, "virtualconsole: [parameters]\n\t-b [bios image]\n" );
		return 1;
	}

	ram_image = malloc( ram_amt );
	mmio_image = malloc( mmio_size );
	framebuffer_buffer = (uint32_t *) malloc(FRAMEBUFFER_X * FRAMEBUFFER_Y * sizeof(uint32_t));
	framebuffer_addr = (uint32_t *)(mmio_image + 0x100);
	if( !ram_image )
	{
		fprintf( stderr, "Error: could not allocate system image.\n" );
		return -4;
	}
	if( !mmio_image ) 
	{
		fprintf( stderr, "Error: could not allocate mmio image.\n" );
		return -4;
	}
	if (framebuffer_buffer == NULL) {
		fprintf(stderr, "Can't reserve framebuffer mem.\n");
		return 1;
	}
restart:
	{
		FILE * f = fopen( bios_file_name, "rb" );
		if( !f || ferror( f ) )
		{
			fprintf( stderr, "Error: \"%s\" not found\n", bios_file_name );
			return -5;
		}
		fseek( f, 0, SEEK_END );
		long flen = ftell( f );
		fseek( f, 0, SEEK_SET );
		if( flen > ram_amt )
		{
			fprintf( stderr, "Error: Could not fit RAM image (%ld bytes) into %d\n", flen, ram_amt );
			return -6;
		}

		memset( ram_image, 0, ram_amt );
		if( fread( ram_image, flen, 1, f ) != 1)
		{
			fprintf( stderr, "Error: Could not load image.\n" );
			return -7;
		}
		fclose( f );
	}

	window = SDL_CreateWindow("VM Framebuffer",
			          SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED,
				  640, 480,
				  SDL_WINDOW_SHOWN);

	if (window == NULL) {
		fprintf(stderr, "Can't create SDL2 window: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL) {
		fprintf(stderr, "Can't create SDL2 renderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	texture = SDL_CreateTexture(renderer,
				    SDL_PIXELFORMAT_RGBA8888,
				    SDL_TEXTUREACCESS_STATIC,
				    FRAMEBUFFER_X,
				    FRAMEBUFFER_Y);
	if (texture == NULL) {
		fprintf(stderr, "Can't create SDL2 texture: %s\n", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	SDL_Delay(2000);

	CaptureKeyboardInput();

	// The core lives at the end of RAM.
	core = (struct MiniRV32IMAState *)(ram_image + ram_amt - sizeof( struct MiniRV32IMAState ));
	core->pc = MINIRV32_RAM_IMAGE_OFFSET;
	core->regs[10] = 0x00; //hart ID
	core->extraflags |= 3; // Machine-mode.
	// Image is loaded.

	// framebuffer
	uint64_t last_screen_update = 0;

	uint64_t rt;
	uint64_t lastTime = (fixed_update)?0:(GetTimeMicroseconds()/time_divisor);
	int instrs_per_flip = 1024;
	for( rt = 0; rt < instct+1 || instct < 0; rt += instrs_per_flip )
	{
		while (SDL_PollEvent(&event)){
			if (event.type == SDL_QUIT) {
				SDL_DestroyTexture(texture);
				SDL_DestroyRenderer(renderer);
				SDL_DestroyWindow(window);
				SDL_Quit();
				return 0;
			}
		}

		uint64_t tick_start = GetTimeMicroseconds();

		// framebuffer updates 60 hz per second
		uint64_t now = GetTimeMicroseconds();
		if (now - last_screen_update >= framebuffer_interval) {
		    //uint32_t *framebuffer_pointer = (uint32_t *)(mmio_image + 0x100);
		    SDL_UpdateTexture(texture, NULL, framebuffer_buffer, FRAMEBUFFER_X * FRAMEBUFFER_DEPTH);
		    SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xFF);
		    SDL_RenderClear(renderer);
		    SDL_Rect destRect = { (640 - FRAMEBUFFER_X) / 2, (480 - FRAMEBUFFER_Y) / 2, FRAMEBUFFER_X, FRAMEBUFFER_Y };
		    SDL_RenderCopy(renderer, texture, NULL, &destRect);
		    SDL_RenderPresent(renderer);
		    *((uint32_t*)(mmio_image + (0x10038000 - 0x10000000))) = 1;
		    last_screen_update = now;
		}
		uint64_t * this_ccount = ((uint64_t*)&core->cyclel);
		uint32_t elapsedUs = 0;
		if( fixed_update )
			elapsedUs = *this_ccount / time_divisor - lastTime;
		else
			elapsedUs = GetTimeMicroseconds()/time_divisor - lastTime;
		lastTime += elapsedUs;

		int ret = MiniRV32IMAStep( core, ram_image, 0, elapsedUs, instrs_per_flip ); // Execute upto 1024 cycles before breaking out.
		switch( ret )
		{
			case 0: break;
			case 1: if( do_sleep ) MiniSleep(); *this_ccount += instrs_per_flip; break;
			case 3: instct = 0; break;
			case 0x7777: goto restart;	//syscon code for restart
			case 0x5555: printf( "POWEROFF@0x%08x%08x\n", core->cycleh, core->cyclel ); return 0; //syscon code for power-off
			default: printf( "Unknown failure\n" ); break;
		}

		if (LIMITED_CPU) {
		// cpu limit speed	
			uint64_t tick_end = GetTimeMicroseconds();
			uint64_t tick_duration_us = tick_end - tick_start;

			uint64_t expected_us = ((uint64_t)instrs_per_flip * 1000000ULL) / TARGET_HZ;

			if( tick_duration_us < expected_us )
			{
		    		usleep(expected_us - tick_duration_us);
			}
		}
		
	}
	DumpState( core, ram_image);
}


//////////////////////////////////////////////////////////////////////////
// Platform-specific functionality
//////////////////////////////////////////////////////////////////////////


#if defined(WINDOWS) || defined(WIN32) || defined(_WIN32)

#include <windows.h>
#include <conio.h>

#define strtoll _strtoi64

static void CaptureKeyboardInput()
{
	system(""); // Poorly documented tick: Enable VT100 Windows mode.
}

static void ResetKeyboardInput()
{
}

static void MiniSleep()
{
	Sleep(1);
}

static uint64_t GetTimeMicroseconds()
{
	static LARGE_INTEGER lpf;
	LARGE_INTEGER li;

	if( !lpf.QuadPart )
		QueryPerformanceFrequency( &lpf );

	QueryPerformanceCounter( &li );
	return ((uint64_t)li.QuadPart * 1000000LL) / (uint64_t)lpf.QuadPart;
}


static int IsKBHit()
{
	return _kbhit();
}

static int ReadKBByte()
{
	// This code is kind of tricky, but used to convert windows arrow keys
	// to VT100 arrow keys.
	static int is_escape_sequence = 0;
	int r;
	if( is_escape_sequence == 1 )
	{
		is_escape_sequence++;
		return '[';
	}

	r = _getch();

	if( is_escape_sequence )
	{
		is_escape_sequence = 0;
		switch( r )
		{
			case 'H': return 'A'; // Up
			case 'P': return 'B'; // Down
			case 'K': return 'D'; // Left
			case 'M': return 'C'; // Right
			case 'G': return 'H'; // Home
			case 'O': return 'F'; // End
			default: return r; // Unknown code.
		}
	}
	else
	{
		switch( r )
		{
			case 13: return 10; //cr->lf
			case 224: is_escape_sequence = 1; return 27; // Escape arrow keys
			default: return r;
		}
	}
}

#else

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

static void CtrlC(int sig)
{
	DumpState( core, ram_image);
	exit( 0 );
}

// Override keyboard, so we can capture all keyboard input for the VM.
static void CaptureKeyboardInput()
{
	// Hook exit, because we want to re-enable keyboard.
	atexit(ResetKeyboardInput);
	signal(SIGINT, CtrlC);

	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~(ICANON | ECHO); // Disable echo as well
	tcsetattr(0, TCSANOW, &term);
}

static void ResetKeyboardInput()
{
	// Re-enable echo, etc. on keyboard.
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag |= ICANON | ECHO;
	tcsetattr(0, TCSANOW, &term);
}

static void MiniSleep()
{
	usleep(500);
}

static uint64_t GetTimeMicroseconds()
{
	struct timeval tv;
	gettimeofday( &tv, 0 );
	return tv.tv_usec + ((uint64_t)(tv.tv_sec)) * 1000000LL;
}

static int is_eofd;

static int ReadKBByte()
{
	if( is_eofd ) return 0xffffffff;
	char rxchar = 0;
	int rread = read(fileno(stdin), (char*)&rxchar, 1);

	if( rread > 0 ) // Tricky: getchar can't be used with arrow keys.
		return rxchar;
	else
		return -1;
}

static int IsKBHit()
{
	if( is_eofd ) return -1;
	int byteswaiting;
	ioctl(0, FIONREAD, &byteswaiting);
	if( !byteswaiting && write( fileno(stdin), 0, 0 ) != 0 ) { is_eofd = 1; return -1; } // Is end-of-file for 
	return !!byteswaiting;
}


#endif


//////////////////////////////////////////////////////////////////////////
// Rest of functions functionality
//////////////////////////////////////////////////////////////////////////

static uint32_t HandleException( uint32_t ir, uint32_t code )
{
	// Weird opcode emitted by duktape on exit.
	if( code == 3 )
	{
		// Could handle other opcodes here.
	}
	return code;
}

static uint32_t HandleControlStore( uint32_t addy, uint32_t val )
{
	if ( addy > 0x0FFFFFFF && addy < 0x12000001 ) {
		if( addy == 0x10000000 ) //UART 8250 / 16550 Data Buffer
		{
			printf( "%c", val );
			fflush( stdout );
			return 0;
		}
		else if( addy == 0x11004004 ) //CLNT
			core->timermatchh = val;
		else if( addy == 0x11004000 ) //CLNT
			core->timermatchl = val;
		else if( addy == 0x10038004 ) {
			memcpy(framebuffer_buffer, framebuffer_addr, FRAMEBUFFER_SIZE8);
			return 0;
		}
		else if( addy == 0x11100000 ) //SYSCON (reboot, poweroff, etc.)
		{
			core->pc = core->pc + 4;
			return val; // NOTE: PC will be PC of Syscon.
		}
		}
		uint32_t *mmio_store_access = (uint32_t *)(mmio_image + addy - 0x10000000);
		*mmio_store_access = val;
	return 0;
}


static uint32_t HandleControlLoad( uint32_t addy )
{
	if ( addy > 0x0FFFFFFF && addy < 0x12000001 ){
		// Emulating a 8250 / 16550 UART
		if( addy == 0x10000005 )
			return 0x60 | IsKBHit();
		else if( addy == 0x10000000 && IsKBHit() )
			return ReadKBByte();
		else if ( addy == 0x10038000 ) { //framebuffer vblank
			uint32_t *vblank_ptr = (uint32_t *)(mmio_image + (0x10038000 - 0x10000000));
			uint32_t val = *vblank_ptr;
			*vblank_ptr = 0;
			return val;
		}
		else if( addy == 0x1100bffc ) // https://chromitem-soc.readthedocs.io/en/latest/clint.html
			return core->timerh;
		else if( addy == 0x1100bff8 )
			return core->timerl;
		uint32_t *mmio_load_access = (uint32_t *)(mmio_image + addy - 0x10000000);
		return *mmio_load_access;
	}
	return 0;
}

static void HandleOtherCSRWrite( uint8_t * image, uint16_t csrno, uint32_t value )
{
	if( csrno == 0x136 )
	{
		printf( "%d", value ); fflush( stdout );
	}
	if( csrno == 0x137 )
	{
		printf( "%08x", value ); fflush( stdout );
	}
	else if( csrno == 0x138 )
	{
		//Print "string"
		uint32_t ptrstart = value - MINIRV32_RAM_IMAGE_OFFSET;
		uint32_t ptrend = ptrstart;
		if( ptrstart >= ram_amt )
			printf( "DEBUG PASSED INVALID PTR (%08x)\n", value );
		while( ptrend < ram_amt )
		{
			if( image[ptrend] == 0 ) break;
			ptrend++;
		}
		if( ptrend != ptrstart )
			fwrite( image + ptrstart, ptrend - ptrstart, 1, stdout );
	}
	else if( csrno == 0x139 )
	{
		putchar( value ); fflush( stdout );
	}
}

static int32_t HandleOtherCSRRead( uint8_t * image, uint16_t csrno )
{
	if( csrno == 0x140 )
	{
		if( !IsKBHit() ) return -1;
		return ReadKBByte();
	}
	return 0;
}

static void DumpState( struct MiniRV32IMAState * core, uint8_t * ram_image )
{
	uint32_t pc = core->pc;
	uint32_t pc_offset = pc - MINIRV32_RAM_IMAGE_OFFSET;
	uint32_t ir = 0;

	printf( "PC: %08x ", pc );
	if( pc_offset >= 0 && pc_offset < ram_amt - 3 )
	{
		ir = *((uint32_t*)(&((uint8_t*)ram_image)[pc_offset]));
		printf( "[0x%08x] ", ir ); 
	}
	else
		printf( "[xxxxxxxxxx] " ); 
	uint32_t * regs = core->regs;
	printf( "Z:%08x ra:%08x sp:%08x gp:%08x tp:%08x t0:%08x t1:%08x t2:%08x s0:%08x s1:%08x a0:%08x a1:%08x a2:%08x a3:%08x a4:%08x a5:%08x ",
		regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7],
		regs[8], regs[9], regs[10], regs[11], regs[12], regs[13], regs[14], regs[15] );
	printf( "a6:%08x a7:%08x s2:%08x s3:%08x s4:%08x s5:%08x s6:%08x s7:%08x s8:%08x s9:%08x s10:%08x s11:%08x t3:%08x t4:%08x t5:%08x t6:%08x\n",
		regs[16], regs[17], regs[18], regs[19], regs[20], regs[21], regs[22], regs[23],
		regs[24], regs[25], regs[26], regs[27], regs[28], regs[29], regs[30], regs[31] );
}

