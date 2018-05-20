/*
This is a BF, compiler, interpreter and converter.
It can convert to C, x86 assembler and others or it can run the BF itself.

It has five methods of running the program, GNU "lightning" JIT, TCCLIB
complied C code, shared library complied C code, a fast array based interpreter
and a slower profiling and tracing tree based interpreter.

In the simple case the C conversions for BF are:

    >   becomes   ++p;			<   becomes   --p;
    +   becomes   ++*p;			-   becomes   --*p;
    .   becomes   putchar(*p);		,   becomes   *p = getchar();
    [   becomes   while (*p) {		]   becomes   }

Or
    .   becomes   write(1,*p,1);	,   becomes   read(0,*p,1);
Or
    [   becomes   if(*p) do {;		]   becomes   } while(*p);
Or
    [   becomes   if(!*p) goto match;	]   becomes   if(*p) goto match;

Normal (C and other) rules should apply so read() and write() only work
with byte cells, getchar() only works correctly with cells larger than
a byte. The best overall is to not change the cell on EOF even if a '-1'
is available at this cell size so the BF code doesn't have to change.
This is the default.

For best speed you should use an optimising C compiler like GCC, the
interpreter loops especially require perfect code generation for best
speed.  Even with 'very good' code generation the reaction of a modern
CPU is very unpredictable.

If you're generating C code for later, telling this program the Cell size
you will be using allows it to generate better code.

This interpreter does not take notice of any comment character. You should
probably configure your editor's syntax highlighting to show command
characters after your favorite comment marker in a very visible form.

*/

#ifdef __STRICT_ANSI__
#if __STDC_VERSION__ < 199901L
#warning This program needs at least the C99 standard.
#endif
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#if !defined(LEGACYOS) && _POSIX_VERSION >= 200112L
#include <locale.h>
#include <langinfo.h>
#include <wchar.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

/* LLONG_MAX came in after inttypes.h, limits.h is very old. */
#if _POSIX_VERSION >= 199506L || defined(LLONG_MAX)
#include "inttypes.h"
#endif

#include "ov_int.h"
#include "bfi.tree.h"

#ifndef NO_EXT_BE
#include "bfi.run.h"
#define XX 0
#include "bfi.be.def"

#include "bfi.runarray.h"
#include "bfi.runmax.h"
#endif

#ifndef __STDC_ISO_10646__
#warning System is not __STDC_ISO_10646__ unicode I/O disabled.
#endif

#ifndef NO_EXT_BE
#include "clock.h"

enum codestyle { c_default,
#define XX 1
#include "bfi.be.def"
    };
int do_codestyle = c_default;
const char * codestylename[] = { "std"
#define XX 8
#include "bfi.be.def"
};
#else
#define start_runclock(x)
#define finish_runclock(x,y)
#define pause_runclock(x)
#define unpause_runclock(x)
static int * taperam = 0;
#define map_hugeram() (void*)((taperam = tcalloc(memsize-hard_left_limit,sizeof(int)))-hard_left_limit)
#define unmap_hugeram() free(taperam)
#define huge_ram_available 0
#endif

char const * program = "C";
int verbose = 0;
int help_flag = 0;
int noheader = 0;
int rle_input = 0;
int do_run = -1;

int opt_bytedefault = 0;
int opt_level = 2;
int opt_runner = 0;
int opt_no_calc = 0;
int opt_no_litprt = 0;
int opt_no_endif = 0;
int opt_no_kv_recursion = 0;
int opt_no_loop_classify = 0;
int opt_no_kvmov = 0;
int opt_regen_mov = -1;
int opt_pointerrescan = 0;

int hard_left_limit = -1024;
int memsize = 0x100000;	/* Default to 1M of tape if otherwise unspecified. */
int enable_trace = 0;
int debug_mode = 0;
int iostyle = 0; /* 0=ASCII, 1=UTF8, 2=Binary, 3=Integer. */
int eofcell = 0; /* 0=>?, 1=> No Change, 2= -1, 3= 0, 4=EOF, 5=No Input, 6=Abort. */
char * input_string = 0;
char * program_string = 0;
int libc_allows_utf8 = 0;
int default_io = 1;

/*
 * The default values here make the compiler assume that the cell is
 * an integer type greater than or equal to a byte. Eveything should
 * be fine even if the cell size is larger than an int.
 *
 * When the cell size is actually set some more optimisations will be
 * allowed. The masks will be set to the correct values for the cell size.
 * The cell size may be anything between ONE bit and the sizeof an int.
 */
unsigned cell_length = 0;/* Number of bits in cell */
int cell_size = 0;  /* Number of bits in cell IF they fit in an int. */
int cell_mask = ~0; /* Mask using & with this. */
int cell_smask = 0; /* Xor and subtract this; normally MSB of the mask. */
char const * cell_type = "C";	/* Cell C variable type */
int cell_type_iso = 0;	/* Using uintmax_t or similar for above. */
int only_uses_putch = 0;

const char * bfname = "brainfuck";
int curr_line = 0, curr_col = 0;
int cmd_line = 0, cmd_col = 0;
int bfi_num = 0;

#define GEN_TOK_STRING(NAME) "T_" #NAME,
const char* tokennames[] = { TOKEN_LIST(GEN_TOK_STRING) };

struct bfi *bfprog = 0;

struct bfi *opt_run_start, *opt_run_end;

/* Stats */
double run_time = 0, io_time = 0;
int loaded_nodes = 0;
int total_nodes = 0;
int max_indent = 0;
int node_type_counts[TCOUNT+1];
int node_profile_counts[TCOUNT+1];
int min_pointer = 0, max_pointer = 0;
int most_negative_mov = 0, most_positive_mov = 0;
int most_neg_maad_loop = 0;
double profile_hits = 0.0;
int profile_min_cell = 0;
int profile_max_cell = 0;

/* Reading */
void load_file(FILE * ifd, int is_first, int is_last, char * bfstring);
void process_file(void);

/* Building */
void print_tree_stats(void);
void printtreecell(FILE * efd, int indent, struct bfi * n);
void printtree(void);
void calculate_stats(void);
void pointer_scan(void);
void pointer_regen(void);
void quick_scan(void);
void invariants_scan(void);
void trim_trailing_sets(void);
int scan_one_node(struct bfi * v, struct bfi ** move_v);
int find_known_calc_state(struct bfi * v);
int flatten_loop(struct bfi * v, int constant_count);
int classify_loop(struct bfi * v);
int flatten_multiplier(struct bfi * v);
void build_string_in_tree(struct bfi * v);
void * tcalloc(size_t nmemb, size_t size);
struct bfi * add_node_after(struct bfi * p);
void find_known_value_recursion(struct bfi * n, int v_offset,
		struct bfi ** n_found,
		int * const_found_p, int * known_value_p, int * unsafe_p,
		int allow_recursion,
		struct bfi * n_stop,
		int * hit_stop_node_p,
		int * base_shift_p,
		int * n_used_p,
		int * unknown_found_p);
int search_for_update_of_offset(struct bfi *n, struct bfi *v, int n_offset);

/* Printing and running */
void run_tree(void);
void print_codedump(void);
int getch(int oldch);
void putch(int oldch);
void set_cell_size(int cell_bits);

/* Trial run. */
void try_opt_runner(void);
int update_opt_runner(struct bfi * n, int * mem, int offset);

/* Other functions. */
void LongUsage(FILE * fd, const char * errormsg) __attribute__ ((__noreturn__));
void Usage(const char * why) __attribute__ ((__noreturn__));
void UsageOptError(char * why) __attribute__ ((__noreturn__));
void UsageInt64(void) __attribute__ ((__noreturn__));
void UsageCheckInts(FILE *fd);
int checkarg(char * opt, char * arg);
#ifdef _WIN32
int SetupVT100Console(int);
void oututf8(int input_chr);
#endif

void LongUsage(FILE * fd, const char * errormsg)
{
#ifdef NO_EXT_BE
    fprintf(fd, "%s: LITE version\n", program);
#else
    print_banner(fd, program);
#endif
    if(errormsg) {
	fprintf(fd, "%s: %s\n", program, errormsg);
    }
    fprintf(fd, "Usage: %s [options] [BF file]\n", program);
    if (fd != stdout) {
	fprintf(fd, "   -h   Long help message. (Pipe it through more)\n");
	fprintf(fd, "   -v   Verbose, repeat for more.\n");
	fprintf(fd, "   -b   Use byte cells not integer.\n");
	fprintf(fd, "   -z   End of file gives 0. -e=-1, -n=skip.\n");
	UsageCheckInts(fd);
	exit(1);
    }

    printf("    If the file is '-' this will read from the standard input and use\n"
	   "    the character '!' to end the BF program and begin the input to it.\n"
	   "    Note: ! is still ignored inside any BF loop and before the first\n"
	   "    BF instruction has been entered. If the standard input is not a\n"
	   "    terminal the '-' is optional.\n"
	   "\n"
#ifndef NO_EXT_BE
	   "    The default action is to run the code using the fastest option available.\n"
#endif
	   );
    printf("\n");
    printf("   -h   This message.\n");
    printf("   -v   Verbose, repeat for more.\n");
    printf("\n");
    printf("   -r   Run in interpreter.\n");
#ifndef NO_EXT_BE
    printf("        There are two normal interpreters a tree based one with full profiling\n");
    printf("        and tracing support and a faster array based one.\n");
    printf("        The array based one will be used unless:\n");
    printf("            * The -# option is used.\n");
    printf("            * The -T option is used.\n");
    printf("            * Three or more -v options are used.\n");
#endif
    printf("\n");
    printf("   -A   Generate output to be compiled or assembled.\n");
    printf("        (opposite of -r, use -b32 to add C header to default.)\n");
    printf("\n");
#ifndef NO_EXT_BE
#define XX 2
#include "bfi.be.def"
    printf("\n");
#endif
    printf("   -T   Switch to the profiling interpreter and turn on tracing\n");
#ifndef NO_EXT_BE
    printf("        or create trace statements in output C code.\n");
#endif
    printf("   -H   Remove headers in output code\n");
    printf("        Also prevents optimiser assuming the tape starts blank.\n");
    printf("   -#   Use '#' as a debug symbol and append one to end.\n");
    printf("\n");
    printf("   -a   Ascii I/O, filters CR from input%s\n", iostyle==0?" (enabled)":"");
#if defined(__STDC_ISO_10646__) || defined(_WIN32)
    printf("   -u   Wide character (unicode) I/O%s\n", iostyle==1?" (enabled)":"");
#endif
    printf("   -B   Binary I/O, unmodified I/O%s\n", iostyle==2?" (enabled)":"");
    printf("\n");
    printf("   -On  'Optimisation' level.\n");
    printf("   -O0      Turn off all optimisation, just leave RLE.\n");
    printf("   -O1      Only enable pointer motion optimisation.\n");
    printf("   -O2      Allow a few simple optimisations.\n");
    printf("   -O3      Maximum normal level, default.\n");
    printf("   -Orun    When generating code run the interpreter over it first.\n");
    printf("   -m   Turn off all optimisation and RLE.\n");
    printf("\n");
    printf("   -b   Use 8 bit cells.\n");
    printf("   -b16 Use 16 bit cells.\n");
    printf("   -b32 Use 32 bit cells.\n");
    printf("        Default for running now (-r/-rc/-j/-q) is %dbits.\n",
			opt_bytedefault? CHAR_BIT:
			(int)sizeof(int)*CHAR_BIT);
#ifdef DISABLE_BN
    printf("        Other bitwidths also work (including 1..32 and maybe 64/128)\n");
#else
    printf("        Any other bitwidth will work too (1..2048 tested)\n");
#endif
    printf("        Full Unicode characters need 21 bits.\n");
#ifndef NO_EXT_BE
    printf("        The default for generated C code is to use the largest type found\n");
    printf("        when it's compiled. It can be controlled with -DC=uintmax_t.\n");
    printf("        NASM can only be 8bit.\n");
    printf("        The optimiser will be less effective if this is not specified.\n");
#endif
    printf("\n");
    printf("   -E   End of file processing.\n");
    printf("   -n  -E1  End of file gives no change for ',' command.\n");
    printf("   -e  -E2  End of file gives -1.\n");
    printf("   -z  -E3  End of file gives 0.\n");
    printf("   -E4      End of file gives EOF (normally -1 too).\n");
    printf("   -E5      Disable ',' command, ie: treat it as a comment character.\n");
    printf("   -E6      Treat running the ',' command as an error and Stop.\n");
    printf("\n");
    printf("   -I'xx'   Use the string as the input to the BF program.\n");
    printf("   -P'pgm'  The string pgm is the first part of the BF program.\n");
    printf("\n");
    printf("General extras\n");
    printf("   -fintio\n");
    printf("        Use decimal I/O instead of character I/O.\n");
    printf("        Specify before -b1 for cell sizes below 7 bits.\n");
    printf("   -mem %d\n", memsize);
    if (!huge_ram_available)
	printf("        Define allocation size for tape memory.\n");
    else {
	printf("        Define array size for generated code.\n");
	printf("        The '-r' option always uses a huge array.\n");
    }
    printf("\n");
    printf("Optimisation extras\n");
    printf("   -fno-negtape\n");
    printf("        Limit optimiser to no negative tape positions.\n");
    printf("   -fno-calctok\n");
    printf("        Disable the T_CALC token.\n");
    printf("   -fno-endif\n");
    printf("        Disable the T_IF and T_ENDIF tokens.\n");
    printf("   -fno-litprt\n");
    printf("        Disable the T_CHR token and printf() strings.\n");
    printf("   -fno-kv-recursion\n");
    printf("        Disable recursive known value optimisations.\n");
    printf("   -fno-loop-classify\n");
    printf("        Disable loop classification optimisations.\n");
    printf("   -fno-kv-mov\n");
    printf("        Disable T_MOV known value optimisations.\n");
    printf("   -fno-loop-offset\n");
    printf("        Regenerate pointer move tokens to remove offsets to loops.\n");
    printf("   -fpointer-rescan\n");
    printf("        Rerun the pointer scan pass after other optimisations.\n");
#ifndef NO_EXT_BE
    printf("\n");
    printf("C generation extras\n");
    printf("   -dynmem\n");
    printf("        Use relloc() to extend tape in generated C code.\n");
#if !defined(DISABLE_DLOPEN)
    printf("   -cc clang\n");
    printf("        Choose C compiler to compile C for '-r' option.\n");
    printf("   -ldl\n");
    printf("        Use dlopen() shared libraries to run C code.\n");
    printf("   -fonecall\n");
    printf("        Call C compiler once to both compile and link.\n");
    printf("        (Normally it is done in two parts to support ccache).\n");
    printf("   -fPIC -fpic -fno-pic\n");
    printf("        Control usage of position independed code flag.\n");
    printf("   -fleave-temps\n");
    printf("        Do not delete C code and library file when done.\n");
    printf("   -ffunct\n");
    printf("        Generate multiple functions for BF code.\n");
    printf("   -fno-funct\n");
    printf("        Generate BF code in a single function.\n");
    printf("   -fgoto\n");
    printf("        Use gotos not while loops for [].\n");
#endif
#if !defined(DISABLE_TCCLIB)
    printf("   -ltcc\n");
    printf("        Use the TCCLIB library to run C code.\n");
#endif
    printf("\n");
#ifdef BE_NASM
    printf("Asm generation options, these adjust the '-s' option.\n");
    printf("   -fnasm (default)\n");
    printf("        Code for NASM which can either be used to directly\n");
    printf("        generate a Linux executable or can be linked by GCC.\n");
    printf("   -fgas\n");
    printf("        Generate code for 'gcc -m32 x.s'.\n");
    printf("   -fwin32\n");
    printf("        Alter -fgas code for Windows.\n");
    printf("\n");
#endif
    printf("    NOTE: Some of the code generators are quite limited and do\n");
    printf("    not support all these options. NASM is 8 bits only. The BF\n");
    printf("    generator does not include the ability to generate most of\n");
    printf("    the optimisation facilities.  The dc(1) generator defaults\n");
    printf("    to -E6 as standard dc(1) has no character input routines.\n");
    printf("    Many (including dc(1)) don't support -E2,-E3 and -E4.\n");
    printf("    Most code generators only support binary I/O.\n");
#endif

    UsageCheckInts(stdout);
    exit(1);
}

void
UsageCheckInts(FILE *fd)
{
    int x, flg = sizeof(int) * 8;
    for(x=1;x+2>=x && --flg;x=(x|(x<<1))); /* -fwrapv must be enabled */
    if (!flg)
	fprintf(fd, "\nBEWARE: The C compiler does NOT use twos-complement arithmetic.\n");
}

void
Usage(const char * why)
{
    LongUsage(stderr, why);
}

void
UsageOptError(char * why)
{
    char buf[256];
    sprintf(buf, "Unknown option '%.200s'", why);
    LongUsage(stderr, buf);
}

void
UsageInt64(void)
{
    fprintf(stderr,
	"Directly available cell sizes are 1..%d", (int)(sizeof(int)*CHAR_BIT));
#if !defined(NO_EXT_BE)
    if (sizeof(long) > sizeof(int))
	fprintf(stderr, ",%d", (int)sizeof(long)*CHAR_BIT);
#if (defined(UINTMAX_MAX) || defined(__SIZEOF_INT128__))
#if defined(UINTMAX_MAX)
    if (sizeof(uintmax_t) > sizeof(long))
	fprintf(stderr, ",%d", (int)sizeof(uintmax_t)*CHAR_BIT);
#endif
#if defined(__SIZEOF_INT128__)
    fprintf(stderr, ",%d", (int)sizeof(__int128)*CHAR_BIT);
#endif
#endif
#endif
    fprintf(stderr, " bits.\n");

#if !defined(NO_EXT_BE)
    fprintf(stderr,
	"The generated C can be configured to use other types "
	"using the 'C' #define.\n"
	"The dc(1) generator can have unlimited size cells\n");
#endif

    exit(1);
}

int
checkarg(char * opt, char * arg)
{
    int arg_is_num = 0;
    if (opt[1] == '-' && opt[2] && opt[3]) return checkarg(opt+1, arg);
    if (arg && ((arg[0] >= '0' && arg[0] <= '9') || arg[0] == '-'))
	arg_is_num = 1;

    if (opt[2] == 0) {
	switch(opt[1]) {
	case 'h': help_flag++; break;
	case 'v': verbose++; break;
	case 'H': noheader=1; break;
	case 'R': rle_input=1; break;
	case '#': debug_mode=1; break;
	case 'r': do_run=1; break;
	case 'A': do_run=0; break;
#ifndef NO_EXT_BE
#define XX 3
#include "bfi.be.def"
#endif
	case 'm': opt_level= -1; break;
	case 'T': enable_trace=1; break;

	case '8':
	    iostyle=2;
	    default_io=0;
	    set_cell_size(8);
	    eofcell = 4;
	    break;

	case 'a': iostyle=0; default_io=0; break;
	case 'B': iostyle=2; default_io=0; break;
#if defined(__STDC_ISO_10646__) || defined(_WIN32)
	case 'u':
#ifndef __WIN32
	    if (!libc_allows_utf8 && verbose)
		fprintf(stderr, "WARNING: POSIX compliant libc has 'fixed' UTF-8.\n");
#endif
	    iostyle=1;
	    default_io=0;
	    break;
#endif

	case 'O':
	    if (!arg_is_num) { opt_level = 1; break; }
	    opt_level = strtol(arg,0,10);
	    return 2;
	case 'E':
	    if (!arg_is_num) {
		eofcell = 4;	/* tape[cell] = getchar(); */
		return 1;
	    } else
		eofcell=strtol(arg,0,10);
	    return 2;
	case 'n': eofcell = 1; break;
	case 'e': eofcell = 2; break;
	case 'z': eofcell = 3; break;

	case 'b':
	    if (!arg_is_num) {
		set_cell_size(8);
		return 1;
	    } else {
		set_cell_size(strtol(arg,0,10));
		return 2;
	    }
	case 'P': case 'I':
	    if (arg == 0)
		return 0;
	    else
	    {
		unsigned len = 0, z;
		char * str;
		if (opt[1]=='I') str=input_string; else str=program_string;
		z = (str!=0);
		if (z) len += strlen(str);
		len += strlen(arg) + 2;
		str = realloc(str, len);
		if(!str) {
		    perror("realloc"); exit(1);
		}
		if (!z) *str = 0;
		strcat(str, arg);
		strcat(str, "\n");
		if (opt[1]=='I') input_string=str; else program_string=str;
	    }
	    return 2;
	default:
	    return 0;
	}
	return 1;

    } else if (!strcmp(opt, "-Orun")) {
	opt_runner = 1;
	return 1;
    } else if (!strcmp(opt, "-fno-negtape")) { hard_left_limit = 0; return 1;
    } else if (!strcmp(opt, "-fno-calctok")) { opt_no_calc = 1; return 1;
    } else if (!strcmp(opt, "-fno-endif")) { opt_no_endif = 1; return 1;
    } else if (!strcmp(opt, "-fno-litprt")) { opt_no_litprt = 1; return 1;
    } else if (!strcmp(opt, "-fno-kv-recursion")) { opt_no_kv_recursion = 1; return 1;
    } else if (!strcmp(opt, "-fno-loop-classify")) { opt_no_loop_classify = 1; return 1;
    } else if (!strcmp(opt, "-fno-kv-mov")) { opt_no_kvmov = 1; return 1;
    } else if (!strcmp(opt, "-fpointer-rescan")) { opt_pointerrescan = 1; return 1;
    } else if (!strcmp(opt, "-fno-loop-offset")) { opt_regen_mov = 1; return 1;
    } else if (!strcmp(opt, "-floop-offset")) { opt_regen_mov = 0; return 1;
    } else if (!strcmp(opt, "-fintio")) { iostyle = 3; opt_no_litprt = 1; default_io=0; return 1;
    } else if (!strcmp(opt, "-help")) { help_flag++; return 1;
    } else if (!strcmp(opt, "-mem")) {
	if (arg == 0 || arg[0] < '0' || arg[0] > '9') {
	    memsize = 30000;
	    return 1;
	} else {
	    memsize = strtol(arg,0,10);
	    return 2;
	}
    }

#ifndef NO_EXT_BE
#define XX 9
#include "bfi.be.def"
#endif

    return 0;
}

int
main(int argc, char ** argv)
{
    int ar, opton=1;
    const char *p;
    const char ** filelist = 0;
    int filecount = 0;

    program = argv[0];
    if ((p=strrchr(program, '/')) != 0)
	program = p+1;

    opt_bytedefault = !strcmp(program, "bf");

#ifdef __STDC_ISO_10646__
#if !defined(LEGACYOS) && _POSIX_VERSION >= 200112L
    setlocale(LC_CTYPE, "");
    if (!opt_bytedefault)
	if (!strcmp("UTF-8", nl_langinfo(CODESET))) libc_allows_utf8 = 1;
#endif

    if (opt_bytedefault) iostyle = 2;
    else if (libc_allows_utf8) iostyle = 1;
    else iostyle = 0;
#endif
#ifdef _WIN32
    SetupVT100Console(0);
#endif

#if !defined(ALLOW_INSANITY) && defined(__GNUC__) \
    && (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ > 3)
    {
	int x, flg = sizeof(int) * 8;
	for(x=1;x+1>x && --flg;x=(x|(x<<1)));
	if (!flg) {
	    fprintf(stderr, "Compile failure, we need sane integers\n");
	    fprintf(stderr, "Please use the -fwrapv option when compiling.\n");
	    exit(255);
	}
    }
#endif

    filelist = calloc(argc, sizeof*filelist);

    for(ar=1; ar<argc; ar++) {
	if (opton && argv[ar][0] == '-' && argv[ar][1] != 0) {
	    char optbuf[4];
	    int f;
	    if (argv[ar][1] == '-' && argv[ar][2] == 0) {
		opton = 0;
		continue;
	    }
	    if (argv[ar+1] && argv[ar+1][0] != '-')
		f = checkarg(argv[ar], argv[ar+1]);
	    else
		f = checkarg(argv[ar], 0);
	    if (f == 1) continue;
	    if (f == 2) { ar++; continue; }

	    optbuf[0] = '-';
	    optbuf[1] = argv[ar][1];
	    optbuf[2] = 0;
	    if ((f = checkarg(optbuf, argv[ar]+2)) == 2) continue;

	    for(p=argv[ar]+1+(f==1); *p; p++) {
		optbuf[0] = '-';
		optbuf[1] = *p;
		optbuf[2] = 0;

		if (checkarg(optbuf, 0) != 1)
		    UsageOptError(argv[ar]);
	    }
	} else
	    filelist[filecount++] = argv[ar];
    }

#ifdef _WIN32
    SetupVT100Console(iostyle);
#endif

    if(help_flag)
	LongUsage(stdout, 0);

    if(!program_string && filecount == 0) {
	if (isatty(STDIN_FILENO))
	    Usage("Error: Filename or '-' should be specified");
	filelist[filecount++] = "-";
    }

#ifndef NO_EXT_BE
#define XX 4
#include "bfi.be.def"

    if (do_run == -1) do_run = (do_codestyle == c_default);
#endif

    if (do_run) opt_runner = 0; /* Run it in one go */
    if (cell_length==0 && (do_run || opt_runner))
	set_cell_size(-1);

#ifndef NO_EXT_BE
#define XX 6
#include "bfi.be.def"
#endif

    {
	int first=1;

	if (filecount == 1 && strcmp(filelist[0], "-"))
	    bfname = filelist[0];	/* From argv */

	if(program_string) {
	    load_file(0, 1, (filecount==0), program_string);
	    first = 0;
	}

	for(ar = 0; ar<filecount; ar++) {
	    FILE * ifd;
	    const char * fname = filelist[ar];

	    if (!fname || strcmp(fname, "-") == 0) ifd = stdin;
	    else if ((ifd = fopen(fname, "r")) == 0) {
		perror(fname);
		exit(1);
	    }

	    load_file(ifd, first, ar+1>=filecount, 0);
	    first = 0;

	    if (ifd!=stdin) fclose(ifd);
	}
	free(filelist); filelist = 0;
    }

    process_file();

#ifdef _WIN32
    SetupVT100Console(2);
#endif

    delete_tree();
    exit(0);
}

/*
 * Load a file into the tree.
 * The tree can be preseeded with some init code if required.
 * Loops and comment loops can start in one file and finish in another.
 *
 * Line numbers are reset for each file.
 * For the last file (is_last set) the jump tables are checked and some
 * cleanups and warnings are done on the tree.
 */
void
load_file(FILE * ifd, int is_first, int is_last, char * bfstring)
{
    int ch, lid = 0;
    struct bfi *p=0, *n=bfprog;

static struct bfi *jst;
static int dld, inp, out, rle = 0, num = 1, ov_flg;

    if (is_first) { jst = 0; dld = 0; }
    if (n) { while(n->next) n=n->next; p = n;}

    curr_line = 1; curr_col = 0;

    while((ifd	? (ch = getc(ifd)) != EOF
		: (bfstring && *bfstring ? !!(ch= *bfstring++) :0)
	    ) && (ifd!=stdin || ch != '!' || !inp || dld || jst )) {
	int c = 0;
	if (ch == '\n') { curr_line++; curr_col=0; }
	else curr_col ++;

	if (rle_input && ch>='0' && ch<='9') {
	    if (rle == 0) {
		num = ch-'0';
		rle = 1;
		ov_flg = 0;
	    } else
		num = ov_iadd(ov_imul(num, 10, &ov_flg), ch-'0', &ov_flg);
	    continue;
	}
	rle = 0;
	if (ch == ' ' || ch == '\n' || ch == '\t') continue;

	/* A long run of digits is ignored. */
	if (ov_flg) {
	    num = 1; ov_flg = 0;
	    if (rle_input)
		fprintf(stderr,
		    "Warning: numeric overflow in prefix at Line %d, Col %d\n",
		    curr_line, curr_col);
	}

	switch(ch) {
	case '>': ch = T_MOV; c=  num; break;
	case '<': ch = T_MOV; c= -num; break;
	case '+': ch = T_ADD; c=  num; break;
	case '-': ch = T_ADD; c= -num; break;
	case '[': ch = T_WHL; break;
	case ']': ch = T_END; break;
	case '.': ch = T_PRT; out = 1; break;
	case ',':
	    if (eofcell == 5) ch = T_NOP;
	    else if (eofcell == 6) ch = T_STOP;
	    else { ch = T_INP; inp = 1; }
	    break;
	case '#':
	    if(debug_mode) ch = T_DUMP; else ch = T_NOP;
	    break;
	case '=':
	    if(rle_input) ch = T_SET; else ch = T_NOP;
	    break;
	default:  ch = T_NOP; break;
	}
	num = 1;

	if (ch != T_NOP) {
#ifndef NO_PREOPT   /* Simple syntax optimisation */
	    if (opt_level>=0) {
		/* Comment loops, can never be run */
		/* This BF code isn't just dead it's been buried in soft peat
		 * for three months and recycled as firelighters. */

		if (dld ||  (ch == T_WHL && p==0 && !noheader) ||
			    (ch == T_WHL && p!=0 && p->type == T_END)   ) {
		    if (ch == T_WHL) dld ++;
		    if (ch == T_END) dld --;
		    continue;
		}
		/* RLE compacting of instructions. This will be done by later
		 * passes, but it's cheaper to do it here. */
		if (c && p && ch == p->type){
		    int ov_flg_rle = 0, t = ov_iadd(p->count, c, &ov_flg_rle);
		    if (!ov_flg_rle) {
			p->count = t;
			continue;
		    }
		}
	    }
#endif
	    n = tcalloc(1, sizeof*n);
	    n->inum = bfi_num++; n->line = curr_line; n->col = curr_col;
	    if (p) { p->next = n; n->prev = p; } else bfprog = n;
	    n->type = ch; p = n;
	    if (n->type == T_WHL) { n->jmp=jst; jst = n; }
	    else if (n->type == T_END) {
		if (jst) { n->jmp = jst; jst = jst->jmp; n->jmp->jmp = n;
		} else n->type = T_ERR;
	    } else
		n->count = c;
	}
    }

    if (!is_last) return;

    if (debug_mode && (p && p->type != T_DUMP)) {
	n = tcalloc(1, sizeof*n);
	n->inum = bfi_num++;
	n->line = curr_line;
	if (p) { p->next = n; n->prev = p; } else bfprog = n;
	n->type = T_DUMP; p = n;
    }

    /* I could make this close the loops, Better? */
    while (jst) { n = jst; jst = jst->jmp; n->type = T_ERR; n->jmp = 0; }

    if (dld)
	fprintf(stderr, "Warning: Unterminated comment loop at end of file.\n");

    loaded_nodes = 0;
    for(n=bfprog; n; n=n->next) {
	switch(n->type)
	{
	    case T_ADD:
	    case T_MOV:
		if(n->count == 0) n->type = T_NOP;
		break;
	    case T_WHL: n->count = ++lid; break;
	    case T_ERR: /* Unbalanced bkts */
		fprintf(stderr,
			"Warning: unbalanced bracket at Line %d, Col %d\n",
			n->line, n->col);
		n->type = T_NOP;
		break;
	}
	loaded_nodes++;
	n->orgtype = n->type;
    }
    total_nodes = loaded_nodes;
}

void *
tcalloc(size_t nmemb, size_t size)
{
    void * m;
    m = calloc(nmemb, size);
    if (m) return m;

#if __STDC_VERSION__ >= 199901L && _XOPEN_VERSION >= 600
    fprintf(stderr, "Allocate of %zu*%zu bytes failed, ABORT\n", nmemb, size);
#else
    fprintf(stderr, "Allocate of %lu*%lu bytes failed, ABORT\n",
	    (unsigned long)nmemb, (unsigned long)size);
#endif
    exit(42);
}

struct bfi *
add_node_after(struct bfi * p)
{
    struct bfi * n = tcalloc(1, sizeof*n);
    n->inum = bfi_num++;
    n->type = T_NOP;
    n->orgtype = T_NOP;
    if (p) {
	n->line = p->line;
	n->col = p->col;
	n->prev = p;
	n->next = p->next;
	if (n->next) n->next->prev = n;
	n->prev->next = n;
    } else if (bfprog) {
	n->next = bfprog;
	if (n->next) n->next->prev = n;
    } else bfprog = n;

    return n;
}

void
printtreecell(FILE * efd, int indent, struct bfi * n)
{
    int i = indent;
    while(i-->0)
	fprintf(efd, " ");
    if (n == 0) {fprintf(efd, "NULL Cell"); return; }
    if (n->type >= 0 && n->type < TCOUNT)
	fprintf(efd, "%s", tokennames[n->type]);
    else
	fprintf(efd, "TOKEN(%d)", n->type);
    switch(n->type)
    {
    case T_MOV:
	if(n->offset == 0)
	    fprintf(efd, " %d ", n->count);
	else
	    fprintf(efd, " %d,(off=%d), ", n->count, n->offset);
	break;
    case T_ADD: case T_SET:
	fprintf(efd, "[%d]:%d, ", n->offset, n->count);
	break;
    case T_CALC:
	if (n->count3 == 0) {
	    fprintf(efd, "[%d] = %d + [%d]*%d, ",
		n->offset, n->count, n->offset2, n->count2);
	} else if (n->offset == n->offset2 && n->count2 == 1) {
	    fprintf(efd, "[%d] += [%d]*%d + %d, ",
		n->offset, n->offset3, n->count3, n->count);
	} else
	    fprintf(efd, "[%d] = %d + [%d]*%d + [%d]*%d, ",
		n->offset, n->count, n->offset2, n->count2,
		n->offset3, n->count3);
	break;

    case T_PRT:
	fprintf(efd, "[%d], ", n->offset);
	break;

    case T_CHR:
	if (n->count >= ' ' && n->count <= '~' && n->count != '"')
	    fprintf(efd, " '%c', ", n->count);
	else
	    fprintf(efd, " 0x%02x, ", n->count);
	break;

    case T_INP: fprintf(efd, "[%d], ", n->offset); break;
    case T_DEAD: fprintf(efd, "if(0) id=%d, ", n->count); break;
    case T_STOP: fprintf(efd, "[%d], ", n->offset); break;

    case T_WHL: case T_IF: case T_MULT: case T_CMULT:
	fprintf(efd, "[%d],id=%d, ", n->offset, n->count);
	break;

    case T_END: case T_ENDIF:
	fprintf(efd, "[%d] id=%d, ", n->offset, n->jmp->count);
	break;

    default:
	fprintf(efd, "[%d]:%d, ", n->offset, n->count);
	if (n->offset2 != 0 || n->count2 != 0)
	    fprintf(efd, "x2[%d]:%d, ", n->offset2, n->count2);
	if (n->offset3 != 0 || n->count3 != 0)
	    fprintf(efd, "x3[%d]:%d, ", n->offset3, n->count3);
    }
    fprintf(efd, "$%d, ", n->inum);
    if (indent>=0) {
	if(n->next == 0)
	    fprintf(efd, "next 0, ");
	else if(n->next->prev != n)
	    fprintf(efd, "next $%d, ", n->next->inum);
	if(n->prev == 0)
	    fprintf(efd, "prev 0, ");
	else if(n->prev->next != n)
	    fprintf(efd, "prev $%d, ", n->prev->inum);
	if(n->jmp)
	    fprintf(efd, "jmp $%d, ", n->jmp->inum);
	if(n->prevskip)
	    fprintf(efd, "skip $%d, ", n->prevskip->inum);
	if(n->profile)
	    fprintf(efd, "prof %d, ", n->profile);
	if(n->line || n->col)
	    fprintf(efd, "@(%d,%d)", n->line, n->col);
	else
	    fprintf(efd, "@new");
    } else {
	if(n->jmp)
	    fprintf(efd, "jmp $%d, ", n->jmp->inum);
	if(n->profile)
	    fprintf(efd, "prof %d, ", n->profile);
    }
}

void
printtree(void)
{
    int indent = 0;
    struct bfi * n = bfprog;
    fprintf(stderr, "Whole tree dump...\n");
    while(n)
    {
	fprintf(stderr, " ");
	if (indent>0 && n->orgtype == T_END) indent--;
	fprintf(stderr, " ");
	printtreecell(stderr, indent, n);
	fprintf(stderr, "\n");
	if (n->orgtype == T_WHL) indent++;
	n=n->next;
    }
    fprintf(stderr, "End of whole tree dump.\n");
}

void
process_file(void)
{
#define tickstart() start_runclock()
#define tickend(str) do{ \
	    if (verbose>2) {					\
		double rt = 0;					\
		finish_runclock(&rt,0);				\
		if(rt) fprintf(stderr, str " %.3fs\n",rt);	\
	    } } while(0)

    if (verbose>5) printtree();
    if (opt_level>=1) {
	if (verbose>2)
	    fprintf(stderr, "Starting optimise level %d, cell_size %d\n",
		    opt_level, cell_size);

	tickstart();
	pointer_scan();
	tickend("Time for pointer scan");

	tickstart();
	quick_scan();
	tickend("Time for quick scan");

	if (opt_level>=2) {
	    calculate_stats();
	    if (verbose>5) printtree();

	    tickstart();
	    invariants_scan();
	    tickend("Time for invariant scan");

	    if (opt_pointerrescan) {
		tickstart();
		calculate_stats();
		if (min_pointer < 0) {
		    struct bfi * n = tcalloc(1, sizeof*n);
		    n->inum = bfi_num++;
		    n->type = T_MOV;
		    n->orgtype = T_NOP;
		    n->next = bfprog;
		    n->count = -min_pointer;
		    bfprog = n;
		}
		pointer_scan();
		tickend("Time for second pointer scan");
	    }

	    if (!noheader)
		trim_trailing_sets();
	}

	if (opt_runner) try_opt_runner();

	if (opt_regen_mov != 0)
	    pointer_regen();

	if (verbose>2)
	    fprintf(stderr, "Optimise level %d complete\n", opt_level);
	if (verbose>5) printtree();
    } else
	if (opt_runner) try_opt_runner();

    if (verbose) {
	print_tree_stats();
	if (verbose>1)
	    printtree();
    } else
	calculate_stats(); /* is in print_tree_stats() */

    /* limit to proven memory range. */
    if (node_type_counts[T_MOV] == 0 && max_pointer >= 0)
	memsize = max_pointer+1;

#ifdef NO_EXT_BE
    if (do_run) {
	setbuf(stdout, 0);
	run_tree();
	if (verbose>2) {
	    print_tree_stats();
	    printtree();
	}
	unmap_hugeram();
    } else
	print_codedump();
#else
    if (do_run) {
	if (total_nodes == node_type_counts[T_CHR])
	    do_codestyle = c_default; /* Be lazy for a 'Hello World'. */
	else if (isatty(STDOUT_FILENO))
	    setbuf(stdout, 0);
    }

    if (do_codestyle == c_default) {
	if (do_run) {
	    if (verbose>2 || debug_mode || enable_trace ||
		total_nodes == node_type_counts[T_CHR] ||
		node_type_counts[T_DUMP] != 0) {

		if (total_nodes != node_type_counts[T_CHR] && cell_size <= 0) {
		    fprintf(stderr, "ERROR: cannot run combination: "
				    "%dbit cells%s%s%s.\n",
			cell_length,
			debug_mode? ", debug mode":"",
			enable_trace? ", trace mode":"",
			verbose>2 ? ", profiling enabled":"");
		    exit(1);
		}

		if (verbose)
		    fprintf(stderr, "Starting profiling interpreter\n");
		run_tree();

		if (verbose>2) {
		    print_tree_stats();
		    printtree();
		}
	    } else if (cell_size <= 0) {
		if (verbose>1)
		    fprintf(stderr, "Starting maxtree interpreter\n");
		run_maxtree();
	    } else {
		if (verbose)
		    fprintf(stderr, "Starting array interpreter\n");
		convert_tree_to_runarray();
	    }

	    unmap_hugeram();
	} else
	    print_codedump();
    } else {
	if (do_run) {

	    if (verbose)
		fprintf(stderr, "Running tree using '%s' generator\n",
			codestylename[do_codestyle]);

	    switch(do_codestyle) {
	    default:
		fprintf(stderr, "The '%s' code generator does not "
				"have a direct run option available.\n",
				codestylename[do_codestyle]);
		exit(1);
#define XX 7
#include "bfi.be.def"
	    }

	    unmap_hugeram();
	} else {
	    if (verbose)
		fprintf(stderr, "Generating '%s' style output code\n",
			codestylename[do_codestyle]);

	    switch(do_codestyle) {
	    default:
		fprintf(stderr, "The '%s' code generator does not "
				"have a code output option available.\n",
				codestylename[do_codestyle]);
		exit(1);
#define XX 5
#include "bfi.be.def"
	    }
	}
    }
#endif

    if (do_run && only_uses_putch == 2 && isatty(STDOUT_FILENO)) {
	fflush(stdout);
	fprintf(stderr, "\n");
    }

    if (do_run && verbose && verbose < 3 && run_time>0) {
	fflush(stdout);
	fprintf(stderr, "Run time %.6fs, I/O time %.6fs\n", run_time, io_time);
    }

#undef tickstart
#undef tickend
}

void
calculate_stats(void)
{
    struct bfi * n = bfprog;
    int i;
    int indent = 0;

    total_nodes = 0;
    min_pointer = 0;
    max_pointer = 0;
    most_negative_mov = 0;
    most_positive_mov = 0;
    profile_hits = 0;
    max_indent = 0;

    for(i=0; i<TCOUNT+1; i++)
	node_type_counts[i] = 0;

    while(n)
    {
	int t = n->type;
	total_nodes++;
	if (t < 0 || t >= TCOUNT)
	    node_type_counts[TCOUNT] ++;
	else
	    node_type_counts[t] ++;
	if (t == T_MOV) {
	    if (n->count < most_negative_mov)
		most_negative_mov = n->count;
	    if (n->count > most_positive_mov)
		most_positive_mov = n->count;
	} else if (t != T_CHR) {
	    if (min_pointer > n->offset) min_pointer = n->offset;
	    if (max_pointer < n->offset) max_pointer = n->offset;
	}
	if (n->profile)
	    profile_hits += n->profile;

	if (indent>0 && n->orgtype == T_END) indent--;
	if (indent>max_indent) max_indent = indent;
	if (n->orgtype == T_WHL) indent++;

	n=n->next;
    }
}

void
print_tree_stats(void)
{
    int i, c=0, has_node_profile = 0;
    calculate_stats();

    fprintf(stderr, "Total nodes %d, loaded %d", total_nodes, loaded_nodes);
    fprintf(stderr, " (%dk)\n",
	    (loaded_nodes * (int)sizeof(struct bfi) +1023) / 1024);

    if (total_nodes) {
	fprintf(stderr, "Offset range %d..%d", min_pointer, max_pointer);
	fprintf(stderr, ", Minimum MAAD offset %d\n", most_neg_maad_loop);
	if (profile_hits > 0 && run_time>0)
	    fprintf(stderr, "Run time %.6fs, cycles %.0f, %.3fns/cycle\n",
		run_time, profile_hits, 1000000000.0*run_time/profile_hits);

	fprintf(stderr, "Tokens:");
	for(i=0; i<TCOUNT; i++) {
	    if (node_type_counts[i]) {
		if (c>60) { fprintf(stderr, "\n..."); c = 0; }
		c += fprintf(stderr, "%s%s=%d", c?", ":" ",
			    tokennames[i], node_type_counts[i]);
	    }
	    if (node_profile_counts[i])
		has_node_profile++;
	}
	if (c) fprintf(stderr, "\n");
	if (has_node_profile) {
	    fprintf(stderr, "Token profiles");
	    for(i=0; i<TCOUNT; i++) {
		if (node_profile_counts[i])
		    fprintf(stderr, ", %s=%d",
				tokennames[i], node_profile_counts[i]);
	    }
	    fprintf(stderr, "\n");
	}
	if (profile_min_cell != 0 || profile_max_cell != 0)
	    fprintf(stderr, "Tape cells used %d..%d\n",
		profile_min_cell, profile_max_cell);
    }
}

/*
 * This is a simple tree based interpreter with lots of instrumentation.
 *
 * Running the tree directly is slow, all the counting, tracking and tracing
 * slows it down even more.
 */
void
run_tree(void)
{
    int *p, *oldp;
    struct bfi * n = bfprog;

    if (!opt_runner) only_uses_putch = 1;

    oldp = p = map_hugeram();
    start_runclock();

    while(n){
	{
	    int off = (p+n->offset) - oldp;
	    n->profile++;
	    node_profile_counts[n->type]++;
	    if (n->type != T_MOV) {
		if (off < profile_min_cell) profile_min_cell = off;
		if (off > profile_max_cell) profile_max_cell = off;
	    }

	    if (enable_trace) {
		fprintf(stderr, "P(%d,%d)=", n->line, n->col);
		printtreecell(stderr, -1, n);
		if (n->type == T_MOV)
		    fprintf(stderr, "\n");
		else if (n->type != T_CALC) {
		    if (off >= 0)
			fprintf(stderr, "mem[%d]=%d\n", off, oldp[off]);
		    else
			fprintf(stderr, "mem[%d]= UNDERFLOW\n", off);
		} else {
		    int off2, off3;
		    off2 = (p+n->offset2)-oldp;
		    off3 = (p+n->offset3)-oldp;
		    if (n->offset == n->offset2 && n->count2 == 1) {
			if (n->count3 != 0 && p[n->offset3] == 0)
			    fprintf(stderr, "mem[%d]=%d, BF Skip.\n",
				off3, p[n->offset3]);
			else {
			    fprintf(stderr, "mem[%d]=%d", off, oldp[off]);
			    if (n->count3 != 0)
				fprintf(stderr, ", mem[%d]=%d",
				    off3, p[n->offset3]);
			    if (off < 0)
				fprintf(stderr, " UNDERFLOW\n");
			    else
				fprintf(stderr, "\n");
			}
		    } else {
			fprintf(stderr, "mem[%d]=%d", off, oldp[off]);
			if (n->count2 != 0)
			    fprintf(stderr, ", mem[%d]=%d",
				off2, p[n->offset2]);
			if (n->count3 != 0)
			    fprintf(stderr, ", mem[%d]=%d",
				off3, p[n->offset3]);
			fprintf(stderr, "\n");
		    }
		}
	    }
	}

	switch(n->type)
	{
	    case T_MOV: p += n->count; break;
	    case T_ADD: p[n->offset] += n->count; break;
	    case T_SET: p[n->offset] = n->count; break;
	    case T_CALC:
		p[n->offset] = n->count
			    + n->count2 * p[n->offset2]
			    + n->count3 * p[n->offset3];
		if (n->count2) {
		    int off = (p+n->offset2) - oldp;
		    if (off < profile_min_cell) profile_min_cell = off;
		    if (off > profile_max_cell) profile_max_cell = off;
		}
		if (n->count3) {
		    int off = (p+n->offset3) - oldp;
		    if (off < profile_min_cell) profile_min_cell = off;
		    if (off > profile_max_cell) profile_max_cell = off;
		}
		break;

	    case T_IF: case T_MULT: case T_CMULT:

	    case T_WHL: if(UM(p[n->offset]) == 0) n=n->jmp;
		break;

	    case T_END: if(UM(p[n->offset]) != 0) n=n->jmp;
		break;

	    case T_ENDIF:
		break;

	    case T_PRT:
	    case T_CHR:
	    case T_INP:
	    if (opt_runner) {
		if (n->type == T_INP) {
		    fprintf(stderr, "Error: Trial run optimise hit input command\n");
		    exit(1);
		} else {
		    struct bfi *v = add_node_after(opt_run_end);
		    v->type = T_CHR;
		    v->line = n->line;
		    v->col = n->col;
		    v->count = UM(n->type == T_PRT?p[n->offset]:n->count);
		    opt_run_end = v;
		    if (opt_no_litprt) {
			v->type = T_SET;
			v = add_node_after(opt_run_end);
			v->type = T_PRT;
			v->line = n->line;
			v->col = n->col;
			opt_run_end = v;
		    }
		}
		break;
	    } else {
		switch(n->type)
		{
		case T_CHR:
		    putch(n->count);
		    break;
		case T_PRT:
		    putch(p[n->offset]);
		    break;
		case T_INP:
		    p[n->offset] = getch(p[n->offset]);
		    break;
		}
		break;
	    }

	    case T_STOP:
		if (opt_runner)
		    fprintf(stderr, "Error: Trial run optimise hit T_STOP.\n");
		else
		    fprintf(stderr, "STOP Command executed.\n");
		exit(1);

	    case T_NOP:
		break;

	    case T_DUMP:
		if (!opt_runner) {
		    int i, doff, off = (p+n->offset) - oldp;
		    fflush(stdout); /* Keep in sequence if merged */
		    fprintf(stderr, "P(%d,%d):", n->line, n->col);
		    doff = off - 8;
		    doff &= -4;
		    if (doff <0) doff = 0;
		    fprintf(stderr, "ptr=%d, mem[%d]= ", off, doff);
		    for(i=0; i<16; i++) {
			fprintf(stderr, "%s%s%d",
					i?", ":"",
					doff+i==off?">":"",
					UM(oldp[doff+i]));
		    }
		    fprintf(stderr, "\n");
		}
		break;

	    case T_SUSP:
		n=n->next;
		if( update_opt_runner(n, oldp, p-oldp) )
		    continue;
		else
		    goto break_break;

	    default:
		fprintf(stderr, "Execution error:\n"
		       "Bad node: type %d: ptr+%d, cnt=%d.\n",
			n->type, n->offset, n->count);
		exit(1);
	}
	if (enable_trace && n->type != T_PRT && n->type != T_DUMP &&
		n->type != T_CHR && n->type != T_ENDIF) {
	    int off = (p+n->offset) - oldp;
	    fflush(stdout); /* Keep in sequence if merged */
	    fprintf(stderr, "P(%d,%d):", n->line, n->col);
	    fprintf(stderr, "mem[%d]=%d\n", off, oldp[off]);
	}
	n = n->next;
    }

break_break:;
    finish_runclock(&run_time, &io_time);
}

void
delete_tree(void)
{
    struct bfi * n = bfprog, *p;
    bfprog = 0;
    while(n) { p = n; n=n->next; free(p); }
}

/*
 *  This routine relocates T_MOV instructions past other instruction types
 *  in an effort to merge the T_MOVs together and hopefully have them cancel
 *  out completely.
 */
void
pointer_scan(void)
{
    struct bfi * n = bfprog, *n2, *n3, *n4;

    if (verbose>1)
	fprintf(stderr, "Merging pointer movements.\n");

    while(n){

	if (verbose>4 && n->type == T_MOV) {
	    fprintf(stderr, "Checking pointer ...\n");
	    printtreecell(stderr, 1, n);
	    if (verbose>5) {
		fprintf(stderr,  "\n prev ...\n");
		printtreecell(stderr, 2, n->prev);
	    }
	    fprintf(stderr,  "\n next ...\n");
	    printtreecell(stderr, 2, n->next);
	    if (n->next && verbose>5) {
		fprintf(stderr, "\n next->next ...\n");
		printtreecell(stderr, 4, n->next->next);
		if (n->next && n->next->jmp) {
		    fprintf(stderr, "\n next->jmp->prev ...\n");
		    printtreecell(stderr, 4, n->next->jmp->prev);
		    fprintf(stderr, "\n next->jmp ...\n");
		    printtreecell(stderr, 4, n->next->jmp);
		}
	    }
	    fprintf(stderr, "\n");
	}

	if (n->type == T_MOV && n->next) {
	    switch(n->next->type) {
	    case T_MOV:
		if(verbose>4)
		    fprintf(stderr, "Merge movements.\n");
		/* Merge movments */
		n2 = n->next;
		n->count += n2->count;
		n->next = n2->next;
		if (n2->next) n2->next->prev = n;
		free(n2);
		if (n->count == 0 && n->prev) {
		    n2 = n->prev;
		    n2->next = n->next;
		    if (n->next) n->next->prev = n2;
		    free(n);
		    n = n2;
		}
		continue;

	    case T_PRT: case T_CHR: case T_INP:
	    case T_ADD: case T_SET:
	    case T_DUMP:
		if(verbose>4)
		    fprintf(stderr, "Push past command.\n");
		/* Put movement after a normal cmd. */
		n2 = n->next;
		n2->offset += n->count;
		n3 = n->prev;
		n4 = n2->next;
		if(n3) n3->next = n2; else bfprog = n2;
		n2->next = n;
		n->next = n4;
		n2->prev = n3;
		n->prev = n2;
		if(n4) n4->prev = n;
		continue;

	    case T_CALC:
		if(verbose>4)
		    fprintf(stderr, "Push past command.\n");
		/* Put movement after a normal cmd. */
		n2 = n->next;
		n2->offset += n->count;
		if (n2->count2)
		    n2->offset2 += n->count;
		if (n2->count3)
		    n2->offset3 += n->count;
		n3 = n->prev;
		n4 = n2->next;
		if(n3) n3->next = n2; else bfprog = n2;
		n2->next = n;
		n->next = n4;
		n2->prev = n3;
		n->prev = n2;
		if(n4) n4->prev = n;
		continue;

	    case T_STOP:
		if(verbose>4)
		    fprintf(stderr, "Push past command.\n");
		/* Put movement after a normal cmd. */
		n2 = n->next;
		n3 = n->prev;
		n4 = n2->next;
		if(n3) n3->next = n2; else bfprog = n2;
		n2->next = n;
		n->next = n4;
		n2->prev = n3;
		n->prev = n2;
		if(n4) n4->prev = n;
		continue;

	    case T_WHL: case T_IF: case T_MULT: case T_CMULT:
		if(verbose>4)
		    fprintf(stderr, "Push round a loop.\n");
		/* Push this record past the entire loop */
		/* Add two balancing records inside the loop at the start
		 * and end. Hopefully they'll cancel. */

		if (n->count) {
		    /* Insert record at loop start */
		    n2 = n->next;
		    n2->offset += n->count;

		    n4 = add_node_after(n2);
		    n4->type = n->type;
		    n4->count = n->count;

		    /* Insert record at loop end */
		    n4 = add_node_after(n->next->jmp->prev);
		    n4->type = n->type;
		    n4->count = -n->count;
		}

		/* Move this record past loop */
		n2 = n->prev;
		if(n2) n2->next = n->next; else bfprog = n->next;
		if (n->next) n->next->prev = n2;

		n4 = n;
		n2 = n->next->jmp;
		n3 = n2->next;
		n = n->next;
		n2->offset += n4->count;

		n2->next = n4;
		if(n3) n3->prev = n4;
		n4->next = n3;
		n4->prev = n2;
		break;

	    case T_END: case T_ENDIF:
	    default:
		/* Stuck behind an end loop, can't push past this */
		/* Make the line & col of the movement the same as the
		 * T_END that's blocked it. */
		n->line = n->next->line;
		n->col = n->next->col;
		break;
	    }
	}
	/* Push it off the end of the program */
	if (n->type == T_MOV && n->next == 0 && n->prev) {
	    if(verbose>4)
		fprintf(stderr, "Pushed to end of program.\n");

	    n->line = n->prev->line + 1;
	    n->col = 0;

	    if (!noheader) {
		/* Not interested in the last T_MOV or any T_ADD tokens that
		 * are just before it */
		while(n && (n->type == T_MOV || n->type == T_ADD)) {
		    n->type = T_NOP;
		    n2 = n->prev;
		    if (n2) n2->next = 0; else bfprog = 0;
		    free(n);
		    n = n2;
		}
		continue;
	    }
	}
	n=n->next;
    }
    if(verbose>4)
	fprintf(stderr, "Pointer scan complete.\n");
}

void
pointer_regen(void)
{
    struct bfi *v, *n = bfprog;
    int current_shift = 0;

    calculate_stats();
    if (opt_regen_mov != 1) {
	if (node_type_counts[T_MOV] == 0) return;
	if (min_pointer >= -128 && max_pointer <= 1023) return;
    }

    while(n)
    {
	switch(n->type)
	{
	case T_WHL: case T_MULT: case T_CMULT: case T_END:
	case T_IF: case T_ENDIF:
	    if (n->offset != current_shift) {
		if (n->prev && n->prev->type == T_MOV) {
		    v = n->prev;
		    v->count += n->offset - current_shift;
		} else {
		    v = add_node_after(n->prev);
		    v->type = T_MOV;
		    v->count = n->offset - current_shift;
		}
		current_shift = n->offset;
	    }
	    n->offset -= current_shift;
	    break;

	case T_PRT: case T_INP:
	case T_CHR: case T_DUMP:
	case T_ADD: case T_SET:
	    n->offset -= current_shift;
	    break;

	case T_CALC:
	    n->offset -= current_shift;
	    n->offset2 -= current_shift;
	    n->offset3 -= current_shift;
	    break;

	case T_STOP:
	case T_MOV:
	    break;

	default:
	    fprintf(stderr, "Invalid node pointer regen = %s\n", tokennames[n->type]);
	    exit(1);
	}
	n = n->next;
    }
}

void
quick_scan(void)
{
    struct bfi * n = bfprog, *n2;

    if (verbose>1)
	fprintf(stderr, "Finding 'quick' commands.\n");

    while(n){
	/* Looking for "[-]" or "[+]" */
	/* The loop classifier won't pick up [+] */
	if( n->type == T_WHL && n->next && n->next->next &&
	    n->next->type == T_ADD &&
	    n->next->next->type == T_END &&
	    n->offset == n->next->offset &&
	    ( n->next->count == 1 || n->next->count == -1 )
	    ) {
	    /* Change to T_SET */
	    n->next->type = T_SET;
	    n->next->count = 0;

	    /* Delete the loop. */
	    n->type = T_NOP;
	    n->next->next->type = T_NOP;

	    /* If followed by a matching T_ADD merge that in too */
	    n2 = n->next->next->next;
	    if (n2 && n2->type == T_ADD && n2->offset == n->next->offset) {
		int ov_flg=0, t = ov_iadd(n->next->count, n2->count, &ov_flg);
		if (!ov_flg) {
		    n->next->count = t;
		    n2->type = T_NOP;
		}
	    }

	    if(verbose>4) {
		fprintf(stderr, "Replaced T_WHL [-/+] with T_SET.\n");
		printtreecell(stderr, 1, n->next);
		fprintf(stderr, "\n");
	    }
	}

	/* Looking for "[]" the Trivial infinite loop, replace it with
	 * conditional abort.
	 */
	if( n->type == T_WHL && n->next &&
	    (n->next->type == T_END ||
	     (n->next->type == T_NOP && n->next->next && n->next->next->type == T_END)
	    )){

	    /* Insert a T_STOP */
	    n2 = add_node_after(n);
	    n2->type = T_STOP;

	    /* Insert a T_SET because the T_STOP doesn't return so the T_WHL
	     * can be converted to a T_IF later. */
	    n2 = add_node_after(n2);
	    n2->type = T_SET;
	    n2->offset = n->offset;
	    n2->count = 0;

	    if(verbose>4) {
		fprintf(stderr, "Inserted a T_STOP in a [].\n");
		printtreecell(stderr, 1, n->next);
		fprintf(stderr, "\n");
	    }
	}

	/* Clean up the T_NOPs */
	if (n && n->type == T_NOP) {
	    n2 = n; n = n->next;
	    if(n2->prev) n2->prev->next = n; else bfprog = n;
	    if(n) n->prev = n2->prev;
	    free(n2);
	    continue;
	}
	n=n->next;
    }
}

void
invariants_scan(void)
{
    struct bfi * n = bfprog, *n2, *n3, *n4;
    int node_changed;

    if (verbose>1)
	fprintf(stderr, "Scanning for invariant code.\n");

    while(n){
	node_changed = 0;

	switch(n->type) {
	case T_PRT:
	    node_changed = scan_one_node(n, &n);
	    if (node_changed && n->type == T_CHR) {
		n2 = n;
		n = n->next;
		build_string_in_tree(n2);
	    }
	    break;

	case T_SET:
	case T_ADD:
	case T_WHL:
	    node_changed = scan_one_node(n, &n);
	    break;

	case T_CALC:
	    node_changed = find_known_calc_state(n);
	    if (node_changed && n->prev)
		n = n->prev;
	    break;

	case T_END: case T_ENDIF:
	    n2 = n->jmp;
	    n3 = n2;
	    node_changed = scan_one_node(n2, &n3);
	    if (!node_changed)
		node_changed = scan_one_node(n, &n3);
	    if (!node_changed && n->type == T_END)
		    node_changed = classify_loop(n2);
	    if (!node_changed && (n2->type == T_MULT || n2->type == T_CMULT))
		    node_changed = flatten_multiplier(n2);

	    if (node_changed)
		n = n3;
	    break;

	}

	if (n && n->type == T_DEAD && n->jmp) {
	    if (verbose>6) {
		fprintf(stderr, "Removing: ");
		printtreecell(stderr, 0,n);
		fprintf(stderr, "\n");
	    }

	    n2 = n->jmp;
	    n3 = n;
	    n = n2->next;
	    if (n3->prev) n3->prev->next = n; else bfprog = n;
	    if (n) n->prev = n3->prev;

	    n2->next = 0;
	    while(n3) {
		n4 = n3->next;
		free(n3);
		n3 = n4;
	    }
	    node_changed = 1;
	}

	if (n && n->type == T_NOP) {
	    if (verbose>6) {
		fprintf(stderr, "Removing: ");
		printtreecell(stderr, 0,n);
		fprintf(stderr, "\n");
	    }

	    n2 = n; n = n->next;
	    if(n2->prev) n2->prev->next = n; else bfprog = n;
	    if(n) n->prev = n2->prev;
	    free(n2);
	    node_changed = 1;
	}

	if (!node_changed) {
	    n=n->next;
	}
	else if (node_changed && n && n->prev)
	    n = n->prev;
    }
}

void
trim_trailing_sets(void)
{
    struct bfi * n = bfprog, *lastn = 0;
    int min_offset = 0, max_offset = 0;
    int node_changed, i;
    int search_back = 1;
    while(n)
    {
	if (n->type == T_MOV) search_back = 0; /* Why was this now ? */

	if (min_offset > n->offset) min_offset = n->offset;
	if (max_offset < n->offset) max_offset = n->offset;
	lastn = n;
	n=n->next;
    }

    /* Remove obvious deletions. */
    while(lastn) {
	if (lastn->type == T_SET ||
	    lastn->type == T_ADD ||
	    lastn->type == T_CALC) {

	    n = lastn;
	    lastn = lastn->prev;
	    if(lastn) lastn->next = 0;

	    if (n->prev) n->prev->next = 0; else bfprog = 0;
	    n->type = T_NOP;
	    free(n);
	} else
	    break;
    }

    /* Try for some less obvious ones */
    if (!search_back || max_offset - min_offset > 32) return;
    if (verbose>2)
	fprintf(stderr, "Running trailing set scan for %d..%d\n",
			min_offset, max_offset);
    if (verbose>5) printtree();

    if (lastn) {
	lastn = add_node_after(lastn);
	n = add_node_after(lastn);

	for(i=min_offset; i<= max_offset; i++) {
	    n->type = T_SET;
	    n->count = -1;
	    n->offset = i;
	    node_changed = scan_one_node(n, 0);
	    if (node_changed && n->type != T_SET) {
		n->type = T_SET;
		n->count = 1;
		n->offset = i;
		node_changed = scan_one_node(n, 0);
	    }
	}
	lastn->next = 0;
	free(n);
	if (lastn->prev) lastn->prev->next = 0; else bfprog = 0;
	free(lastn);
    }
}

void
find_known_value_recursion(struct bfi * n, int v_offset,
		struct bfi ** n_found,
		int * const_found_p, int * known_value_p, int * unsafe_p,
		int allow_recursion,
		struct bfi * n_stop,
		int * hit_stop_node_p,
		int * base_shift_p,
		int * n_used_p,
		int * unknown_found_p)
{
    /*
     * n		Node to start the search
     * v_offset		Which offset are we interested in
     * allow_recursion  If non-zero we can split on a loop.
     * n_stop		Node to stop the search
     *
     * n_found		Node we found IF it's safe to change/delete
     * const_found_p	True if we found a known value
     * known_value_p	The known value.
     * unsafe_p		True if this value is unsuitable for loop deletion.
     * hit_stop_node_p	True if we found n_stop.
     */
    int unmatched_ends = 0;
    int const_found = 0, known_value = 0, non_zero_unsafe = 0;
    int n_used = 0;
    int distance = 0;
    int base_shift = 0;

    if (opt_no_kv_recursion) allow_recursion = 0;

    if (hit_stop_node_p) *hit_stop_node_p = 0;
    if (n_used_p) n_used = *n_used_p;

    if (verbose>5) {
	fprintf(stderr, "Called(%d): Checking value for offset %d starting: ",
		SEARCHDEPTH-allow_recursion, v_offset);
	printtreecell(stderr, 0,n);
	if (hit_stop_node_p) {
	    fprintf(stderr, ", stopping: ");
	    printtreecell(stderr, 0,n_stop);
	}
	fprintf(stderr, "\n");
    }
    while(n)
    {
	if (verbose>6) {
	    fprintf(stderr, "Checking: ");
	    printtreecell(stderr, 0,n);
	    fprintf(stderr, "\n");
	}
	switch(n->type)
	{
	case T_NOP:
	case T_CHR:
	    break;

	case T_SET:
	    if (n->offset == v_offset) {
		/* We don't know if this node will be hit */
		if (unmatched_ends) goto break_break;

		known_value = n->count;
		const_found = 1;
		goto break_break;
	    }
	    break;

	case T_CALC:
	    /* Nope, if this were a constant it'd be downgraded already */
	    if (n->offset == v_offset)
		goto break_break;
	    if ((n->count2 != 0 && n->offset2 == v_offset) ||
	        (n->count3 != 0 && n->offset3 == v_offset))
		n_used = 1;
	    break;

	case T_PRT:
	    if (n->offset == v_offset) {
		n_used = 1;
		goto break_break;
	    }
	    break;

	case T_INP:
	    if (n->offset == v_offset) {
		n_used = 1;
		goto break_break;
	    }
	    break;

	case T_ADD:	/* Possible instruction merge for T_ADD */
	    if (n->offset == v_offset)
		goto break_break;
	    break;

	case T_MOV:
	    if (unmatched_ends || opt_no_kvmov) {
		if(verbose>5) {
		    fprintf(stderr, "Search blocked by: ");
		    printtreecell(stderr, 0,n);
		    fprintf(stderr, "\n");
		}
		*unknown_found_p = 1;
		goto break_break;
	    }

	    v_offset += n->count;
	    base_shift += n->count;
	    break;

	case T_END:
	    if (n->offset == v_offset) {
		/* We don't know if this node will be hit */
		if (unmatched_ends) goto break_break;

		known_value = 0;
		const_found = 1;
		n_used = 1;
		goto break_break;
	    }
	    /*FALLTHROUGH*/

	case T_ENDIF:
	    if (n->jmp->offset == v_offset) {
		n_used = 1;
	    }
#ifndef NO_RECURSION
	    if (!allow_recursion) {
		unmatched_ends++;
		break;
	    }
#endif

#ifndef NO_RECURSION
	    /* Check both n->jmp and n->prev
	     * If they are compatible we may still have a known value.
	     */
	    if (allow_recursion) {
		int known_value2 = 0, non_zero_unsafe2 = 0, hit_stop2 = 0, base_shift2 = 0;

		if (verbose>5) fprintf(stderr, "%d: T_END: searching for double known\n", __LINE__);
		find_known_value_recursion(n->prev, v_offset,
		    0, &const_found, &known_value2, &non_zero_unsafe2,
		    allow_recursion-1, n->jmp, &hit_stop2, &base_shift2, &n_used,
		    unknown_found_p);
		if (const_found) {
		    if (verbose>5) fprintf(stderr, "%d: Const found in loop.\n", __LINE__);
		    hit_stop2 = 0;
		    find_known_value_recursion(n->jmp->prev, v_offset,
			0, &const_found, &known_value, &non_zero_unsafe,
			allow_recursion-1, n_stop, &hit_stop2, &base_shift2, &n_used,
			unknown_found_p);

		    if (hit_stop2 && known_value2 == 0) {
			const_found = 1;
			known_value = known_value2;
			non_zero_unsafe = non_zero_unsafe2;
		    } else if (const_found && known_value != known_value2) {
			if (verbose>5) fprintf(stderr, "%d: Two known values found %d != %d\n", __LINE__,
				known_value, known_value2);
			const_found = 0;
			*unknown_found_p = 1;
		    }
		} else if (hit_stop2 && base_shift2 == 0) {
		    if (verbose>5) fprintf(stderr, "%d: Nothing found in loop; continuing.\n", __LINE__);

		    n_used = 1;

		    n = n->jmp;
		    break;
		}
		else if (verbose>5) fprintf(stderr, "%d: No constant found.\n", __LINE__);

		n = 0;
	    }
	    goto break_break;
#else
	    unmatched_ends++;
	    break;
#endif

	case T_IF:
	    /* Can search past an IF because it has only one entry point. */
	    if (n->offset == v_offset)
		n_used = 1;
	    if (unmatched_ends>0)
		unmatched_ends--;
	    else
		n_used = 1; /* All MAY be used because we might get skipped */
	    break;

	case T_MULT:
	case T_CMULT:
	case T_WHL:
	    if (n->offset == v_offset)
		n_used = 1;
	    if (unmatched_ends>0)
		unmatched_ends--;

#ifndef NO_RECURSION
	    /* Check both n->jmp and n->prev
	     * If they are compatible we may still have a known value.
	     */
	    if (allow_recursion) {
		int known_value2 = 0, non_zero_unsafe2 = 0, hit_stop2 = 0, base_shift2 = 0;

		if (verbose>5) fprintf(stderr, "%d: WHL: searching for double known\n", __LINE__);
		find_known_value_recursion(n->jmp->prev, v_offset,
		    0, &const_found, &known_value2, &non_zero_unsafe2,
		    allow_recursion-1, n, &hit_stop2, &base_shift2, &n_used,
		    unknown_found_p);
		if (const_found) {
		    if (verbose>5) fprintf(stderr, "%d: Const found in loop.\n", __LINE__);
		    hit_stop2 = 0;
		    find_known_value_recursion(n->prev, v_offset,
			0, &const_found, &known_value, &non_zero_unsafe,
			allow_recursion-1, n_stop, &hit_stop2, &base_shift2, &n_used,
			unknown_found_p);

		    if (hit_stop2) {
			const_found = 0;
			*unknown_found_p = 1;
		    } else if (const_found && known_value != known_value2) {
			if (verbose>5) fprintf(stderr, "%d: Two known values found %d != %d\n", __LINE__,
				known_value, known_value2);
			const_found = 0;
			*unknown_found_p = 1;
		    }
		} else if (hit_stop2 && base_shift2 == 0) {
		    if (verbose>5) fprintf(stderr, "%d: Nothing found in loop; continuing.\n", __LINE__);

		    break;
		}
		else if (verbose>5) fprintf(stderr, "%d: No constant found.\n", __LINE__);

		n = 0;
	    }
	    goto break_break;
#else
	    /*FALLTHROUGH*/
#endif
	default:
	    if(verbose>5) {
		fprintf(stderr, "Search blocked by: ");
		printtreecell(stderr, 0,n);
		fprintf(stderr, "\n");
	    }
	    *unknown_found_p = 1;
	    goto break_break;
	}
	if (n->prevskip)
	    n=n->prevskip;
	else
	    n=n->prev;

	if (n_stop && n_stop == n) {
	    /* Hit the 'stop' node */
	    *hit_stop_node_p = 1;
	    n = 0;
	    goto break_break;
	}

	/* How did we get this far! */
	if (++distance > SEARCHRANGE) goto break_break;
    }

    known_value = 0;
    const_found = (!noheader);
    n_used = 1;

break_break:
    if (const_found && cell_size == 0 && cell_length <= sizeof(int)*CHAR_BIT
	&& known_value != 0 && (known_value&0xFF) == 0) {
	/* Only known if we know the final cell size so ... */
	non_zero_unsafe = 1;
	/* The problem also occurs for other values, but only zero can
	 * be used to detect dead code and change the flow of control.
	 * all other substitutions are cell size agnostic. */
    }
    if (cell_mask > 0)
	known_value = SM(known_value);

    *const_found_p = const_found;
    *known_value_p = known_value;
    *unsafe_p = non_zero_unsafe;
    if (base_shift_p) *base_shift_p = base_shift;
    if (n_used_p) *n_used_p = n_used;
    if (n_found) {
	/* If "n" is safe to modify return it too. */
	if (n==0 || n_used || unmatched_ends || distance > SEARCHRANGE/4*3)
	    *n_found = 0;
	else
	    *n_found = n;
    }

    if (verbose>5) {
	if (const_found)
	    fprintf(stderr, "Returning(%d): Known value is %d%s",
		    SEARCHDEPTH-allow_recursion, known_value,
		    non_zero_unsafe?" (unsafe zero)":"");
	else
	    fprintf(stderr, "Returning(%d): Unknown value for offset %d",
		    SEARCHDEPTH-allow_recursion, v_offset);
	if (hit_stop_node_p)
	    fprintf(stderr, ", hitstop=%d", *hit_stop_node_p);
	if (base_shift_p)
	    fprintf(stderr, ", base_shift=%d", *base_shift_p);
	fprintf(stderr, "\n  From ");
	printtreecell(stderr, 0,n);
	fprintf(stderr, "\n");
    }
}

void
find_known_value(struct bfi * n, int v_offset,
		struct bfi ** n_found,
		int * const_found_p, int * known_value_p, int * unsafe_p)
{
    int unknown_found = 0;
    find_known_value_recursion(n, v_offset,
		n_found, const_found_p, known_value_p, unsafe_p,
		SEARCHDEPTH, 0, 0, 0, 0, &unknown_found);

    if (unknown_found) {
	if (verbose>5)
	    fprintf(stderr, "Returning the true unknown for offset %d\n",
		    v_offset);

	*const_found_p = 0;
	*known_value_p = 0;
	*unsafe_p = 1;
	if (n_found) *n_found = 0;
    }
}

int
scan_one_node(struct bfi * v, struct bfi ** move_v UNUSED)
{
    struct bfi *n = 0;
    int const_found = 0, known_value = 0, non_zero_unsafe = 0;

    if(v == 0) return 0;

    if (verbose>5) {
	fprintf(stderr, "Find state for: ");
	printtreecell(stderr, 0,v);
	fprintf(stderr, "\n");
    }

    find_known_value(v->prev, v->offset,
		&n, &const_found, &known_value, &non_zero_unsafe);

    if (!const_found) {
	switch(v->type) {
	    case T_ADD:
		if (n && n->offset == v->offset &&
			(n->type == T_ADD || n->type == T_CALC) ) {
		    if (cell_size<=0) {
			int ov_flg=0;
			(void)ov_iadd(n->count, v->count, &ov_flg);
			if (ov_flg) return 0;
		    }
		    n->count += v->count;
		    if (cell_mask > 0) n->count = SM(n->count);
		    v->type = T_NOP;
		    if (verbose>5) fprintf(stderr, "  Delete this node.\n");
		    if (n->count == 0 && n->type == T_ADD) {
			struct bfi *n2;

			n->type = T_NOP;
			n2 = n; n = n->prev;
			if (n) n->next = n2->next; else bfprog = n2->next;
			if (n2->next) n2->next->prev = n;
			free(n2);
			if (verbose>5) fprintf(stderr, "  And delete old node.\n");
		    }
		    return 1;
		}
		if (v->count == 0) {
		    /* WTH! this is a nop, label it as such */
		    v->type = T_NOP;
		    if (verbose>5) fprintf(stderr, "  NOP this node.\n");
		    return 1;
		}
		break;
	    case T_SET:
		if (n &&
			( (n->type == T_ADD && n->offset == v->offset) ||
			  (n->type == T_CALC && n->offset == v->offset)
			)) {

		    struct bfi *n2;

		    n->type = T_NOP;
		    n2 = n; n = n->prev;
		    if (n) n->next = n2->next; else bfprog = n2->next;
		    if (n2->next) n2->next->prev = n;
		    free(n2);
		    if (verbose>5) fprintf(stderr, "  Delete old node.\n");
		    return 1;
		}
		break;
	    case T_IF:
		{
		    /* Nested T_IF with same condition ? */
		    struct bfi *n2 = v;
		    while(n2->prev &&
			(n2->prev->type == T_ADD || n2->prev->type == T_SET || n2->prev->type == T_CALC) &&
			    n2->prev->offset != v->offset)
			n2 = n2->prev;
		    if (n2) n2 = n2->prev;
		    if (n2 && n2->offset == v->offset && ( n2->orgtype == T_WHL )) {
			if (verbose>5) fprintf(stderr, "  Nested duplicate T_IF\n");

			v->type = T_NOP;
			v->jmp->type = T_NOP;
			return 1;
		    }
		    if (v->jmp == v->next) {
			if (verbose>5) fprintf(stderr, "  Empty T_IF\n");
			v->type = T_NOP;
			v->jmp->type = T_NOP;
			return 1;
		    }
#if 0
		    /* TODO: Need to check that the cell we're copying from hasn't changed (Like SSA) */
		    if (n2 && n2->offset == v->offset && n2->type == T_CALC &&
			n2->count == 0 && n2->count2 == 1 && n2->count3 == 0) {
			if (verbose>5) {
			    fprintf(stderr, "  Assignment of Cond for T_IF is copy.\n    ");
			    printtreecell(stderr, 0, n2);
			    fprintf(stderr, "\n");
			}
			v->offset = n2->offset2;
			if (move_v) *move_v = n2;
			return 1;
		    }
#endif
		}
		break;
	}

	return 0;
    }

    switch(v->type) {
	case T_ADD: /* Promote to T_SET. */
	    if (cell_size <= 0) {
		int ov_flg=0;
		(void) ov_iadd(v->count, known_value, &ov_flg);
		if (ov_flg) break;
	    }
	    v->type = T_SET;
	    v->count += known_value;
	    if (cell_mask > 0)
		v->count = SM(v->count);
	    if (verbose>5) fprintf(stderr, "  Upgrade to T_SET.\n");
	    /*FALLTHROUGH*/

	case T_SET: /* Delete if same or unused */
	    if (known_value == v->count) {
		v->type = T_NOP;
		if (verbose>5) fprintf(stderr, "  Delete this T_SET.\n");
		return 1;
	    } else if (n && (n->type == T_SET || n->type == T_CALC)) {
		struct bfi *n2;

		if (verbose>5) {
		    fprintf(stderr, "  Delete: ");
		    printtreecell(stderr, 0, n);
		    fprintf(stderr, "\n");
		}
		n->type = T_NOP;
		n2 = n; n = n->prev;
		if (n) n->next = n2->next; else bfprog = n2->next;
		if (n2->next) n2->next->prev = n;
		free(n2);
		return 1;
	    }
	    break;

	case T_PRT: /* Print literal character. */
	    if (opt_no_litprt) break; /* BE can't cope */
	    known_value = UM(known_value);
	    if (known_value < 0 && (known_value&0xFF) < 0x80)
		known_value &= 0xFF;
	    v->type = T_CHR;
	    v->count = known_value;
	    if (verbose>5) fprintf(stderr, "  Make literal putchar.\n");
	    return 1;

	case T_IF: case T_MULT: case T_CMULT:
	case T_WHL: /* Change to (possible) constant loop or dead code. */
	    if (!non_zero_unsafe) {
		if (known_value == 0) {
		    v->type = T_DEAD;
		    if (verbose>5) fprintf(stderr, "  Change loop to T_DEAD (and delete).\n");
		    return 1;
		}

		/* A T_IF is a simple block */
		if (v->type == T_IF) {
		    v->type = T_NOP;
		    v->jmp->type = T_NOP;
		    if (verbose>5) fprintf(stderr, "  Change T_IF to T_NOP.\n");
		    return 1;
		}

		if (v->type == T_MULT || v->type == T_CMULT || v->type == T_WHL)
		    return flatten_loop(v, known_value);
	    }
	    break;

	case T_END:
	    /*
	     * If the value at the end of a loop is known the 'loop' may be a
	     * T_IF loop whatever it's contents.
	     *
	     * If the known_value is non-zero this is an infinite loop.
	     */
	    if (!non_zero_unsafe) {
		if (known_value == 0 && v->jmp->type != T_IF && !opt_no_endif) {
		    struct bfi *n2;

		    if (verbose>5) fprintf(stderr, "  Change loop to T_IF.\n");
		    v->jmp->type = T_IF;
		    v->type = T_ENDIF;

		    /*
		     * Move the set out of the loop, if it won't move duplicate
		     * it to make searches easier as a T_ENDIF does NOT
		     * guarantee it's cell is zero.
		     */
		    if (n && n->type == T_SET) {
			n->type = T_NOP;
			n2 = n; n = n->prev;
			if (n) n->next = n2->next; else bfprog = n2->next;
			if (n2->next) n2->next->prev = n;
			free(n2);
		    }

		    n2 = add_node_after(v);
		    n2->type = T_SET;
		    n2->count = 0;
		    n2->offset = v->offset;

#if 1
		    /* Also note the loop variable may have been saved as it
		     * was zeroed to restore after the end of the loop.
		     * We could check all though the loop and then make sure
		     * that it wasn't modified or used after. But the BF
		     * technique would be to put the save right at the end
		     * of the 'loop'.
		     */
		    n = v->prev;
		    if (n && n->type == T_CALC && n->count == 0 &&
			n->count2 != 0 && n->count3 == 0 &&
			n->offset2 == v->jmp->offset &&
			n->offset != n->offset2) {
			/* T_CALC: temp += loopidx*N */
			/* Also need to know that the temp will be zero before
			 * the loop so the calculation is okay for zero too. */

			int const_found2=0, known_value2=0, non_zero_unsafe2=0;

			find_known_value(v->jmp->prev, n->offset,
				    0, &const_found2, &known_value2,
				    &non_zero_unsafe2);

			if (!const_found2 || non_zero_unsafe2 ||
			    known_value2 != 0)
			    return 1;

			if (verbose>5) fprintf(stderr, "    Also move save of loop counter.\n");
			/* Okay, move it out, before the T_SET */
			n2 = add_node_after(v);
			n2->type = n->type;
			n2->offset = n->offset;
			n2->offset2 = n->offset2;
			n2->count2 = n->count2;
			n->type = T_NOP;
			return 1;
		    }

		    /* Of course it's unlikely that the programmer zero'd
		     * the temp first! */
		    if (n && n->type == T_CALC && n->count == 0 &&
			n->count2 == 1 && n->offset2 == n->offset &&
			n->count3 != 0 &&
			n->offset3 == v->jmp->offset &&
			n->offset != n->offset3 &&
			v->jmp->next && v->jmp->next->next == n) {
			/* TODO:
			    Check that n->offset3 is not changed within
			    the loop rather than checking for a tiny
			    loop that doesn't have enough room.

  m[3] = 0;
  *m = 1;
  ++m[1];
  if(M(m[1])) {
        *m = 0;
        m[3] = m[1];
  }
  m[1] = m[3];
  m[3] = 0;
  m[2] += *m;
  *m = 0;


  m[3] = 0;
  *m = 1;
  ++m[1];
  if(M(m[1])) *m = 0;
  m[2] += *m;
  *m = 0;


  m[0] = 0;
  ++m[1];
  m[2] += !M(m[1]);
  m[3] = 0;


			*/
			/* T_CALC: temp += loopidx*N */
			/* Also need to know that the temp will be zero before
			 * the loop so the calculation is okay for zero too. */

			int const_found2=0, known_value2=0, non_zero_unsafe2=0;

			find_known_value(v->jmp->prev, n->offset,
				    0, &const_found2, &known_value2,
				    &non_zero_unsafe2);

			if (!const_found2 || non_zero_unsafe2 ||
			    known_value2 != 0)
			    return 1;

			if (verbose>5) fprintf(stderr, "    Also move add of loop counter.\n");
			/* Okay, move it out, before the T_SET */
			n2 = add_node_after(v);
			n2->type = n->type;
			n2->offset = n->offset;
			n2->offset2 = n->offset3;
			n2->count2 = n->count3;
			n->type = T_NOP;
			return 1;
		    }
#endif
		    return 1;
		}
	    }
	    break;

	default:
	    break;
    }

    return 0;
}

int
search_for_update_of_offset(struct bfi *n, struct bfi *v, int n_offset)
{
    while(n!=v && n)
    {
	switch(n->type)
	{
	case T_WHL: case T_IF: case T_MULT: case T_CMULT:

	case T_ADD: case T_SET: case T_CALC:
	    if (n->offset == n_offset)
		return 0;
	    break;
	case T_PRT: case T_CHR: case T_NOP:
	    break;
	default: return 0;  /* Any other type is a problem */
	}
	n=n->next;
    }
    return n != 0;
}

int
find_known_calc_state(struct bfi * v)
{
    int rv = 0;
    struct bfi *n1 = 0;
    int const_found1 = 0, known_value1 = 0, non_zero_unsafe1 = 0;
    struct bfi *n2 = 0;
    int const_found2 = 0, known_value2 = 0, non_zero_unsafe2 = 0;
    int n2_valid = 0;
    struct bfi *n3 = 0;
    int const_found3 = 0, known_value3 = 0, non_zero_unsafe3 = 0;
    int n3_valid = 0;

    if(v == 0) return 0;
    if (v->type != T_CALC) return 0;

    if (verbose>5) {
	fprintf(stderr, "Check T_CALC node: ");
	printtreecell(stderr, 0,v);
	fprintf(stderr, "\n");
    }

    if (v->count2 && v->count3 && v->offset2 == v->offset3) {
	/* This is an unlikely overflow. */
	if (cell_size<=0) {
	    int ov_flg=0;
	    (void) ov_iadd(v->count2, v->count3, &ov_flg);
	    if (ov_flg) {
		if (verbose>5)
		    fprintf(stderr, "T_CALC merge overflow @(%d,%d)\n", v->line, v->col);
		return 0;
	    }
	}

	/* Merge identical sides */
	v->count2 += v->count3;
	v->count3 = 0;
	if (cell_mask > 0) v->count2 = SM(v->count2);
	rv = 1;
    }

    if (v->count3 != 0 && v->offset == v->offset3) {
	/* Swap them so += looks nice and code generation is safer. */
	/* Only offset2 can be the same as offset */
	int t;

	t = v->offset2;
	v->offset2 = v->offset3;
	v->offset3 = t;
	t = v->count2;
	v->count2 = v->count3;
	v->count3 = t;
	rv = 1;
    }

    if ((v->count2 == 0 || v->offset2 != v->offset) &&
        (v->count3 == 0 || v->offset3 != v->offset)) {
	find_known_value(v->prev, v->offset,
		    &n1, &const_found1, &known_value1, &non_zero_unsafe1);
	if (n1 &&
	    (n1->type == T_ADD || n1->type == T_SET || n1->type == T_CALC)) {
	    /* Overidden change, delete it */
	    struct bfi *n4;

	    n1->type = T_NOP;
	    n4 = n1; n1 = n1->prev;
	    if (n1) n1->next = n4->next; else bfprog = n4->next;
	    if (n4->next) n4->next->prev = n1;
	    free(n4);
	    return 1;
	}
    }

    if (v->count2) {
	find_known_value(v->prev, v->offset2,
		    &n2, &const_found2, &known_value2, &non_zero_unsafe2);
	n2_valid = 1;
    }

    if (v->count3) {
	find_known_value(v->prev, v->offset3,
		    &n3, &const_found3, &known_value3, &non_zero_unsafe3);
	n3_valid = 1;
    }

    if (const_found2 && cell_size<=0) {
	/* If the calc might overflow ignore the constant */
	int ov_flg=0;
	(void) ov_iadd(v->count, ov_imul(v->count2, known_value2, &ov_flg), &ov_flg);
	if (ov_flg) {
	    if (verbose>5)
		fprintf(stderr, "T_CALC const2 overflow %d+%d*%d @(%d,%d)\n",
			v->count, v->count2, known_value2, v->line, v->col);
	    const_found2 = 0;
	}
    }

    if (const_found3 && cell_size<=0) {
	/* If the calc might overflow ignore the constant */
	int ov_flg=0;
	(void) ov_iadd(v->count, ov_imul(v->count3, known_value3, &ov_flg), &ov_flg);
	if (ov_flg) {
	    if (verbose>5)
		fprintf(stderr, "T_CALC const3 overflow %d+%d*%d @(%d,%d)\n",
			v->count, v->count3, known_value3, v->line, v->col);
	    const_found3 = 0;
	}
    }

    if (const_found2) {
	v->count += v->count2 * known_value2;
	v->count2 = 0;
	if (cell_mask > 0) v->count = SM(v->count);
	rv = 1;
	n2_valid = 0;
    }

    if (const_found3) {
	v->count += v->count3 * known_value3;
	if (cell_mask > 0) v->count = SM(v->count);
	v->count3 = 0;
	rv = 1;
    }

    /* Make sure offset2 is used first */
    if (v->count2 == 0 && v->count3 != 0) {
	v->offset2 = v->offset3;
	v->count2 = v->count3;
	const_found2 = const_found3;
	known_value2 = known_value3;
	non_zero_unsafe2 = non_zero_unsafe3;
	n2 = n3;
	v->count3 = 0;
	rv = 1;
	n2_valid = n3_valid;
	n3_valid = 0;
    }

    if (n2 && n2->type == T_CALC && n2_valid &&
	    v->count2 == 1 && v->count3 == 0 && v->count == 0) {
	/* A direct assignment from n2 but not a constant */
	/* plus n2 seems safe to delete. */
	int is_ok = 1;

	/* Do we still have the values available that we used to make n2 ? */
	if (is_ok && n2->count2)
	    is_ok = search_for_update_of_offset(n2, v, n2->offset2);

	if (is_ok && n2->count3)
	    is_ok = search_for_update_of_offset(n2, v, n2->offset3);

	if (is_ok) {
	    /* Okay, move it to here, get rid of the double assign. */
	    v->count = n2->count;
	    v->count2 = n2->count2;
	    v->offset2 = n2->offset2;
	    v->count3 = n2->count3;
	    v->offset3 = n2->offset3;
	    if (n2->offset == v->offset) {
		struct bfi *n4;

		n2->type = T_NOP;
		n4 = n2; n2 = n2->prev;
		if (n2) n2->next = n4->next; else bfprog = n4->next;
		if (n4->next) n4->next->prev = n2;
		free(n4);
		if (verbose>5) fprintf(stderr, "  Delete old T_SET.\n");
		if (n3 == n2) n3 = 0;
		n2 = 0;
	    }
	    rv = 1;
	    n2_valid = 0;
	}
    }

    if (n2 && n2->type == T_CALC && n2_valid &&
	    v->count2 == 1 && v->count3 == 0 && v->count == 0 &&
	    n2->next == v && v->next &&
	    v->next->type == T_SET && v->next->offset == n2->offset) {
	/* A direct assignment from n2 and it's the previous node and the next
	 * node wipes it. */
	/* This is a BF standard form. */

	int t;
	t = n2->offset;
	n2->offset = v->offset;
	v->offset = t;
	v->offset2 = n2->offset;
	v->type = T_NOP;
	n2_valid = 0;
	rv = 1;
    }

#if 1
    if (n2 && n2->type == T_CALC && n2_valid && v->next && v->count2 != 0 &&
	    n2->count == 0 && n2->count3 == 0 && n2->next == v &&
	    v->next->offset == n2->offset &&
	    (v->next->type == T_SET ||
	      ( v->next->type == T_CALC &&
		v->next->count3 == 0 &&
		v->next->offset2 != n2->offset)
	    )) {
	/* A simple multiplication from n2 and it's the previous node
	 * and the next node wipes it. */
	/* This is a BF standard form. */

	if (cell_size<=0) {
	    int ov_flg=0;
	    (void) ov_imul(v->count2, n2->count2, &ov_flg);
	    if (ov_flg) {
		if (verbose>5)
		    fprintf(stderr, "T_CALC merge Mult overflow %d*%d @(%d,%d)\n",
			    v->count2, n2->count2, v->line, v->col);
		return rv;
	    }
	}

	v->offset2 = n2->offset2;
	v->count2 = v->count2 * n2->count2;
	if (cell_mask > 0) v->count2 = SM(v->count2);

	n2->type = T_NOP;
	if (n2->prev) n2->prev->next = n2->next; else bfprog = n2->next;
	if (n2->next) n2->next->prev = n2->prev;
	free(n2);
	n2_valid = 0;
	return 1;
    }
#endif

    if (v->count2 == 0 && v->count3 == 0) {
	/* This is now a simple T_SET */
	v->type = T_SET;
	if (cell_mask > 0)
	    v->count = SM(v->count);
	rv = 1;
    }

    if (v->count2 == 1 && v->count3 == 0 && v->offset == v->offset2) {
	/* This is now a simple T_ADD */
	v->type = T_ADD;
	if (cell_mask > 0)
	    v->count = SM(v->count);
	/* Okay, it's actually a T_NOP now */
	if (v->count == 0)
	    v->type = T_NOP;
	rv = 1;
    }

    return rv;
}

/*
 * This function will remove very simple loops that have a constant loop
 * variable on entry.
 */
int
flatten_loop(struct bfi * v, int constant_count)
{
    struct bfi *n, *dec_node = 0;
    int is_znode = 0;
    int loop_step = 0;
    int have_mult = 0;
    int have_add = 0;
    int have_set = 0;

    n = v->next;
    while(1)
    {
	if (n == 0) return 0;
	if (n->type == T_END || n->type == T_ENDIF) break;
	if (n->type != T_ADD && n->type != T_SET) {
	    if (n->offset == v->offset) return 0;
	    if (have_mult || have_add || have_set) return 0;
	    if (n->type != T_CALC) return 0;
	    if (constant_count <= 0 || constant_count > 9) return 0;
	    if (n->count3 != 0 || n->count2 <= 0 || n->count != 0)
		return 0;
	    if (n->offset != n->offset2 || n->count2 > 1000 || n->count2 < 1)
		return 0;
	    /* T_CALC of form X *= 5 with SMALL integer repeats. */
	    have_mult++;
	} else if (n->offset == v->offset) {
	    if (dec_node) return 0; /* Two updates, hmmm */
	    if (n->type != T_ADD) {
		if (n->type != T_SET || n->count != 0) return 0;
		is_znode = 1;
	    }
	    else loop_step = n->count;
	    dec_node = n;
	} else if (n->type == T_ADD) {
	    have_add++;
	} else if (n->type == T_SET)
	    have_set++;
	n=n->next;
    }
    if (dec_node == 0 || (have_mult && (have_add || have_set))) return 0;

    if (!is_znode && loop_step != -1)
    {
	int sum;
	if (loop_step == 0) return 0; /* Infinite ! */
	if (have_mult) return 0;

	/* Not a simple decrement hmmm. */
	/* This sort of sillyness is normally only used for 'wrapping' 8bits so ... */
	if (cell_size == 8) {

	    /* I could solve the modulo division, but for eight bits
	     * it's a lot simpler just to trial it. */
	    unsigned short cnt = 0;
	    sum = constant_count;
	    while(sum && cnt < 256) {
		cnt++;
		sum = ((sum + (loop_step & 0xFF)) & 0xFF);
	    }

	    if (cnt >= 256) return 0;

	    constant_count = cnt;
	    loop_step = -1;
	}

	if (loop_step != -1) {
	    /* of course it might still be exactly divisible ... */
	    if ((loop_step>0) == (constant_count<0) &&
		abs(constant_count) % abs(loop_step) == 0) {

		constant_count = abs(constant_count / loop_step);
		loop_step = -1;
	    }
	}

	/* This detects the code [-]++[>+<++] which generates MAXINT */
	/* We can only resolve this if we know the cell size. */
	if (abs(loop_step) == 2 && cell_size>1 && loop_step == constant_count) {
	    constant_count = ~(-1 << (cell_size-1));	    /* GCC Whinge */
	    loop_step = -1;
	}

	if (loop_step != -1) return 0;
    }

    if (verbose>5)
	fprintf(stderr, "  Loop is start=%d step=%d\n", constant_count, loop_step);

    if (have_mult && constant_count < 0) return 0;

    /* Found a loop with a known value at entry that contains simple operations
     * where the loop variable is decremented nicely. GOOD! */

    if (is_znode)
	constant_count = 1;

    /* Check for overflows. */
    if (cell_size<=0) {
	n = v->next;
	while(1)
	{
	    int newcount = 0;

	    if (n->type == T_END || n->type == T_ENDIF) break;
	    if (n->type == T_ADD) {
		int ov_flg = 0;
		newcount = ov_imul(n->count, constant_count, &ov_flg);
		if (ov_flg) {
		    if (verbose>5) fprintf(stderr, "  Loop calculation overflow @(%d,%d)\n", n->line, n->col);
		    return 0;
		}
	    }
	    if (n->type == T_CALC) {
		/* n->count2 = pow(n->count2, constant_count) */
		int ov_flg=0, i, j = n->count2;
		newcount = j;
		for(i=0; i<constant_count-1 && !ov_flg; i++)
		    newcount = ov_imul(newcount, j, &ov_flg);
		if (ov_flg) {
		    if (verbose>5) fprintf(stderr, "  Loop power calculation overflow @(%d,%d)\n", n->line, n->col);
		    return 0;
		}
	    }
	    n=n->next;
	}
    }

    if (verbose>5) fprintf(stderr, "  Loop replaced with T_NOP. @(%d,%d)\n", v->line, v->col);
    n = v->next;
    while(1)
    {
	if (n->type == T_END || n->type == T_ENDIF) break;
	if (n->type == T_ADD) {
	    n->count = n->count * constant_count;
	    if (cell_mask > 0) n->count = SM(n->count);
	}
	if (n->type == T_CALC) {
	    /* n->count2 = pow(n->count2, constant_count) */
	    int i, j = n->count2;
	    for(i=0; i<constant_count-1; i++)
		n->count2 = n->count2 * j;
	    if (cell_mask > 0) n->count2 = SM(n->count2);
	}
	n=n->next;
    }

    n->type = T_NOP;
    v->type = T_NOP;

    return 1;
}

int
classify_loop(struct bfi * v)
{
    struct bfi *n, *dec_node = 0;
    int is_znode = 0;
    int has_equ = 0;
    int has_add = 0;
    int typewas;
    int most_negoff = 0;
    int nested_loop_count = 0;

    if (opt_no_loop_classify) return 0;
    if(verbose>5) {
	fprintf(stderr, "Classify loop: ");
	printtreecell(stderr, 0, v);
	fprintf(stderr, "\n");
    }

    /* We can only reclassify loops that were loops and STILL ARE */
    if (!v || v->orgtype != T_WHL || v->type == T_IF) return 0;
    typewas = v->type;
    n = v->next;
    while(n != v->jmp)
    {
	if (n == 0 || n->type == T_MOV) return 0;
	if (n->type != T_ADD && n->type != T_SET) {
	    return 0;
	} else {
	    if (n->offset == v->offset) {
		if (dec_node) return 0; /* Two updates, nope! */
		if (nested_loop_count) {
		    if(verbose>5) fprintf(stderr, "Nested loop changes count\n");
		    return 0;
		}
		if (n->type != T_ADD || n->count != -1) {
		    if (n->type != T_SET || n->count != 0) return 0;
		    is_znode = 1;
		}
		dec_node = n;
	    } else {
		if (n->offset-v->offset < most_negoff)
		    most_negoff = n->offset-v->offset;
		if (n->type == T_ADD) has_add = 1;
		if (n->type == T_SET) has_equ = 1;
	    }
	}
	n=n->next;
    }
    if (is_znode && opt_no_endif) return 0;
    if (dec_node == 0) {
	if (verbose>4 && v->type != T_IF)
	    fprintf(stderr, "Possible Infinite loop at %d:%d\n", v->line, v->col);
	return 0;
    }

    /* Found a loop that contains only T_ADD and T_SET where the loop
     * variable is decremented nicely. This is a multiply loop. */

    /* The loop decrement isn't at the end; move it there. */
    if (dec_node != v->jmp->prev || is_znode) {
	struct bfi *n1, *n2;

	/* Snip it out of the loop */
	n1 = dec_node;
	n2 = n1->prev;
	n2->next = n1->next;
	n2 = n1->next;
	n2->prev = n1->prev;

	/* And put it back, but if this is an if (or unconditional) put it
	 * back AFTER the loop. */
	if (!is_znode) {
	    n2 = v->jmp->prev;
	    n1->next = n2->next;
	    n1->prev = n2;
	    n2->next = n1;
	    n1->next->prev = n1;
	} else {
	    n2 = v->jmp;
	    n1->next = n2->next;
	    n1->prev = n2;
	    n2->next = n1;
	    if(n1->next) n1->next->prev = n1;
	}
    }

    if (!has_add && !has_equ) {
	if (verbose>5) fprintf(stderr, "Loop flattened to single run @(%d,%d)\n", v->line, v->col);
	n->type = T_NOP;
	v->type = T_NOP;
	dec_node->type = T_SET;
	dec_node->count = 0;
    } else if (is_znode) {
	if (verbose>5) fprintf(stderr, "Loop flattened to T_IF @(%d,%d)\n", v->line, v->col);
	v->type = T_IF;
	v->jmp->type = T_ENDIF;
    } else {
	if (verbose>5) fprintf(stderr, "Loop flattened to multiply @(%d,%d)\n", v->line, v->col);
	v->type = T_CMULT;
	/* Without T_SET if all offsets >= Loop offset we don't need the if. */
	/* BUT: Any access below the loop can possibly be before the start
	 * of the tape if there's a T_MOV left anywhere in the program. */
	if (!has_equ) {
	    if (most_negoff >= hard_left_limit ||
		   node_type_counts[T_MOV] == 0) {

		v->type = T_MULT;
		if (most_negoff < most_neg_maad_loop)
		    most_neg_maad_loop = most_negoff;
	    }
	}
    }
    if (verbose>6) {
	fprintf(stderr, "Loop is now: ");
	printtreecell(stderr, 0,v);
	fprintf(stderr, "\n");

	n = v->next;
	while(n != v->jmp)
	{
	    fprintf(stderr, "\t: ");
	    printtreecell(stderr, 0,n);
	    fprintf(stderr, "\n");
	    n = n->next;
	}
    }
    return (v->type != typewas);
}

/*
 * This function will generate T_CALC tokens for simple multiplication loops.
 */
int
flatten_multiplier(struct bfi * v)
{
    struct bfi *n, *dec_node = 0;
    if (v->type != T_MULT && v->type != T_CMULT) return 0;

    if (opt_no_calc || (opt_no_endif && v->type == T_CMULT)) return 0;

    n = v->next;
    while(n != v->jmp)
    {
	if (n->offset == v->offset)
	    dec_node = n;

	if (n->type == T_ADD) {
	    n->type = T_CALC;
	    n->count2 = 1;
	    n->offset2 = n->offset;
	    n->count3 = n->count;
	    n->offset3 = v->offset;
	    n->count = 0;
	}
	n = n->next;
    }

    if (v->type == T_MULT) {
	if (verbose>5) fprintf(stderr, "Multiplier flattened to single run @(%d,%d)\n", v->line, v->col);
	v->type = T_NOP;
	n->type = T_NOP;
    } else {
	if (verbose>5) fprintf(stderr, "Multiplier flattened to T_IF @(%d,%d)\n", v->line, v->col);
	v->type = T_IF;
	n->type = T_ENDIF;
	if (dec_node) {
	    /* Move this node out of the loop. */
	    struct bfi *n1, *n2;

	    /* Snip it out of the loop */
	    n1 = dec_node;
	    n2 = n1->prev;
	    n2->next = n1->next;
	    n2 = n1->next;
	    n2->prev = n1->prev;

	    /* And put it back AFTER the loop. */
	    n2 = n;
	    n1->next = n2->next;
	    n1->prev = n2;
	    n2->next = n1;
	    if(n1->next) n1->next->prev = n1;
	}
    }
    if (verbose>6) {
	fprintf(stderr, "Loop is now: ");
	printtreecell(stderr, 0,v);
	fprintf(stderr, "\n");

	n = v->next;
	while(n != v->jmp)
	{
	    fprintf(stderr, "\t: ");
	    printtreecell(stderr, 0,n);
	    fprintf(stderr, "\n");
	    n = n->next;
	}
    }

    return 1;
}

/*
 * This moves literal T_CHR nodes back up the list to join to the previous
 * group of similar T_CHR nodes. An additional pointer (prevskip) is set
 * so that they all point at the first in the growing 'string'.
 *
 * This allows the constant searching to disregard these nodes in one step.
 *
 * Because the nodes are all together it can also help the code generators
 * build "printf" commands.
 */
void
build_string_in_tree(struct bfi * v)
{
    struct bfi * n = v->prev;
    int found = 0;
    while(n) {
	switch (n->type) {
	default:
	    /* No string but move here */
	    found = 1;
	    break;

	case T_MOV: case T_ADD: case T_CALC: case T_SET: /* Safe */
	    break;

	case T_PRT:
	    return; /* Not a string */

	case T_CHR:
	    found = 1;
	    break;
	}
	if (found) break;
	n = n->prev;
    }

    if (verbose>5) {
	if (n && n->type == T_CHR)
	    fprintf(stderr, "Found string at ");
	else
	    fprintf(stderr, "Found other at ");
	printtreecell(stderr, 0, n);
	fprintf(stderr, "\nAttaching ");
	printtreecell(stderr, 0, v);
	fprintf(stderr, "\n");
    }

    if (n) {
	v->prev->next = v->next;
	if (v->next) v->next->prev = v->prev;
	v->next = n->next;
	v->prev = n;
	if(v->next) v->next->prev = v;
	v->prev->next = v;

	/* Skipping past the whole string with prev */
	if (n->prevskip)
	    v->prevskip = n->prevskip;
	else if (n->type == T_CHR)
	    v->prevskip = n;
    } else if (v->prev) {
	v->prev->next = v->next;
	if (v->next) v->next->prev = v->prev;
	v->next = bfprog;
	bfprog = v;
	if (v->next) v->next->prev = v;
	v->prev = 0;
    }
}

/*
 * This function contains the code for the '-Orun' option.
 * The option simply runs the optimised tree until it finds a loop containing
 * an input instruction. Then everything it's run gets converted onto a print
 * string and some code to setup the tape.
 *
 * For a good benchmark program this should do nothing, but frequently it'll
 * change the compiled program into a 'Hello World'.
 *
 * TODO: If there are NO T_INP instructions we could use a fast runner and
 *      just intercept the output.
 *
 *	I could continue running until we actually hit the T_INP instruction,
 *	I would need to save the current tape state so that it can be rolled
 *	back to the beginning of the loop.
 *
 *	This method could also be used in the middle of the program, just stop
 *	when you find a calculation based on an unknown.
 *
 *	A simple counter would stop this hanging on an infinite loop.
 */
void
try_opt_runner(void)
{
    struct bfi *v = bfprog, *n = 0;
    int lp = 0;

    if (cell_size <= 0) return;	/* Oops! */

    while(v && v->type != T_INP && v->type != T_STOP) {
	if (v->orgtype == T_END) lp--;
	if(!lp && v->orgtype != T_WHL) n=v;
	if (v->orgtype == T_WHL) lp++;
	v=v->next;
    }

    if (n == 0) return;

    if (verbose>5) {
	fprintf(stderr, "Inserting T_SUSP node after: ");
	printtreecell(stderr, 0, n);
	fprintf(stderr, "\nSearch stopped by : ");
	printtreecell(stderr, 0, v);
	fprintf(stderr, "\n");
    }
    v = add_node_after(n);
    v->type = T_SUSP;

    if (verbose>5) printtree();

    opt_run_start = opt_run_end = tcalloc(1, sizeof*bfprog);
    opt_run_start->inum = bfi_num++;
    opt_run_start->type = T_NOP;
    if (verbose>3)
	fprintf(stderr, "Running trial run optimise\n");
    run_tree();
    if (verbose>2) {
	fprintf(stderr, "Trial run optimise finished.\n");
	print_tree_stats();
    }
}

int
update_opt_runner(struct bfi * n, int * mem, int offset)
{
    struct bfi *v = bfprog;
    int i;

    if (n && n->orgtype == T_WHL && UM(mem[offset + n->offset]) == 0) {
	/* Move the T_SUSP */
	int lp = 1;
	if (verbose>3)
	    fprintf(stderr, "Trial run optimise triggered on dead loop.\n");

	v = n->jmp;
	n = 0;

	while(v && v->type != T_INP && v->type != T_STOP) {
	    if (v->orgtype == T_END) lp--;
	    if(!lp && v->orgtype != T_WHL) n=v;
	    if (v->orgtype == T_WHL) lp++;
	    v=v->next;
	}

	if (verbose>5) {
	    fprintf(stderr, "Inserting T_SUSP node after: ");
	    printtreecell(stderr, 0, n);
	    fprintf(stderr, "\nSearch stopped by : ");
	    printtreecell(stderr, 0, v);
	    fprintf(stderr, "\n");
	}

	v = add_node_after(n);
	v->type = T_SUSP;
	return 1;
    }

    if (verbose>3)
	fprintf(stderr, "Trial run optimise triggered.\n");

    while(v!=n) {
	bfprog = v->next;
	v->type = T_NOP;
	free(v);
	v = bfprog;
    }

    if (bfprog) {
	for(i=profile_min_cell; i<=profile_max_cell; i++) {
	    if (UM(mem[i]) != 0) {
		v = add_node_after(opt_run_end);
		v->type = T_SET;
		v->offset = i;
		if (cell_size == 8)
		    v->count = UM(mem[i]);
		else
		    v->count = SM(mem[i]);
		opt_run_end = v;
	    }
	}

	if (offset) {
	    v = add_node_after(opt_run_end);
	    v->type = T_MOV;
	    v->count = offset;
	    opt_run_end = v;
	}
    }

    if (opt_run_start && opt_run_start->type == T_NOP) {
	v = opt_run_start->next;
	if (v) v->prev = 0;
	v = opt_run_start;
	opt_run_start = opt_run_start->next;
	free(v);
    }

    if (opt_run_start) {
	if (opt_no_litprt && bfprog) {
	    struct bfi *v2 = add_node_after(opt_run_end);
	    v2->type = T_SET;
	    v2->count = 0;
	    opt_run_end = v2;
	}
	opt_run_end->next = bfprog;
	bfprog = opt_run_start;
	if (opt_run_end->next)
	    opt_run_end->next->prev = opt_run_end;
    }
    return 0;
}

/*
 * This outputs the tree as a flat language.
 *
 * This can easily be used as macro invocations by any macro assembler or
 * anything that can be translated in a similar way. The C preprocessor
 * has be used successfully to translate it into several different
 * scripting languages (except Python).
 *
 *  bfi -A pgm.b | gcc -include macro.rb.cpp -E -P - > pgm.out
 *
 * The default header generates some C code.
 */
void
print_codedump(void)
{
    struct bfi * n = bfprog;
    int disable_c_header = 0;

    if (cell_size != sizeof(int)*CHAR_BIT &&
	cell_size != sizeof(short)*CHAR_BIT &&
	cell_size != sizeof(char)*CHAR_BIT &&
	cell_length < sizeof(int)*CHAR_BIT) {
	disable_c_header = 1;
	if (verbose)
	    fprintf(stderr, "Code dump C header only works for C types\n");
    }

    if (!noheader && !disable_c_header) {
	printf("%s%s%s\n",
	    "#ifndef bf_start" "\n"
	    "#include <unistd.h>",
	    (cell_length <= sizeof(int)*CHAR_BIT?"":"\n#include <stdint.h>"),
	    "\n#define bf_end() return 0;}");

	printf("%s%s%s\n",
	    "#define bf_start(msz,moff,bits) ",cell_type," mem[msz+moff]; \\");

	if (node_type_counts[T_CHR] || node_type_counts[T_PRT])
	    puts("  void putch(int c) {char s=c; write(1,&s,1);} \\");

	if (node_type_counts[T_CHR])
	    puts("  void putstr(char *s) {char *p=s; while(*p)p++; write(1,s,p-s);}\\");

	printf("%s%s%s\n",
	    "  int main(void){register ",cell_type,"*p=mem+moff;");

	if (enable_trace)
	    puts("#define posn(l,c) /* Line l Column c */");

	/* See:  opt_regen_mov */
	if (node_type_counts[T_MOV])
	    puts("#define ptradd(x) p += x;");

	if (node_type_counts[T_ADD])
	    puts("#define add_i(x,y) p[x] += y;");

	if (node_type_counts[T_SET])
	    puts("#define set_i(x,y) p[x] = y;");

	/* See:  opt_no_calc */
	if (node_type_counts[T_CALC])
	    printf("%s\n",
		"#define add_c(x,y) p[x] += p[y];"
	"\n"	"#define add_cm(x,y,z) p[x] += p[y] * z;"
	"\n"	"#define set_c(x,y) p[x] = p[y];"
	"\n"	"#define set_cm(x,y,z) p[x] = p[y] * z;"
	"\n"	"#define add_cmi(o1,o2,o3,o4) p[o1] += p[o2] * o3 + o4;"
	"\n"	"#define set_cmi(o1,o2,o3,o4) p[o1] = p[o2] * o3 + o4;"
	"\n"	"#define set_tmi(o1,o2,o3,o4,o5,o6) p[o1] = o2 + p[o3] * o4 + p[o5] * o6;");

	if(node_type_counts[T_MULT] || node_type_counts[T_CMULT]
	    || node_type_counts[T_WHL]) {
	    puts("#define lp_start(x,y,c) if(p[x]==0) goto E##y; S##y:");
	    puts("#define lp_end(x,y) if(p[x]) goto S##y; E##y:");
	}

	/* See:  opt_no_endif */
	if (node_type_counts[T_IF]) {
	    puts("#define if_start(x,y) if(p[x]==0) goto E##y;");
	    puts("#define if_end(x) E##x:");
	}

	if (node_type_counts[T_STOP])
	    puts("#define bf_err() exit(1);");

	if (node_type_counts[T_DUMP])
	    puts("#define bf_dump(l,c) /* Hmmm */");

	if (node_type_counts[T_CHR]) {
	    printf("%s\n",
		"#define outchar(x) putch(x);"
	"\n"	"#define outstr(x) putstr(x);");
	}
	if (node_type_counts[T_PRT]) {
	    puts("#define outcell(x) putch(p[x]);");
	}

	if (node_type_counts[T_INP]) {
	    switch(eofcell)
	    {
	    case 0:
	    case 1:
		puts("#define inpchar(x) "
		     "{unsigned char a[1]; if(read(0,a,1)>0) p[x]= *a;}");
		break;
	    case 4:
#if EOF != -1
		printf("#define inpchar(x) "
		     "{unsigned char a[1]; if(read(0,a,1)>0) p[x]= *a;"
		     "else p[x]= %d;}", EOF);
		break;
#endif
	    case 2:
		puts("#define inpchar(x) "
		     "{unsigned char a[1]; if(read(0,a,1)>0) p[x]= *a;"
		     "else p[x]= -1;}");
		break;
	    case 3:
		puts("#define inpchar(x) "
		     "{unsigned char a[1]; if(read(0,a,1)>0) p[x]= *a;"
		     "else p[x]= 0;}");
		break;
	    }
	}

	puts("#endif");
    }

    printf("bf_start(%d,%d,%d)\n", memsize, -most_neg_maad_loop, cell_size);
    while(n)
    {
	if (enable_trace)
	    printf("posn(%d,%3d)\n", n->line, n->col);
	switch(n->type)
	{
	case T_MOV:
	    if (n->count)
		printf("ptradd(%d)\n", n->count);
	    break;

	case T_ADD:
	    if (n->count)
		printf("add_i(%d,%d)\n", n->offset, n->count);
	    break;

	case T_SET:
	    printf("set_i(%d,%d)\n", n->offset, n->count);
	    break;

	case T_CALC:
	    if (n->count == 0) {
		if (n->offset == n->offset2 && n->count2 == 1 && n->count3 == 1) {
		    printf("add_c(%d,%d)\n", n->offset, n->offset3);
		    break;
		}

		if (n->offset == n->offset2 && n->count2 == 1) {
		    printf("add_cm(%d,%d,%d)\n", n->offset, n->offset3, n->count3);
		    break;
		}

		if (n->count3 == 0) {
		    if (n->count2 == 1)
			printf("set_c(%d,%d)\n", n->offset, n->offset2);
		    else
			printf("set_cm(%d,%d,%d)\n", n->offset, n->offset2, n->count2);
		    break;
		}
	    }

	    if (n->offset == n->offset2 && n->count2 == 1) {
		printf("add_cmi(%d,%d,%d,%d)\n",
			n->offset, n->offset3, n->count3, n->count);
		break;
	    }

	    if (n->count3 == 0) {
		printf("set_cmi(%d,%d,%d,%d)\n",
		    n->offset, n->offset2, n->count2, n->count);
	    } else {
		printf("set_tmi(%d,%d,%d,%d,%d,%d)\n",
		    n->offset, n->count, n->offset2, n->count2,
		    n->offset3, n->count3);
	    }
	    break;

#define okay_for_cstr(xc) \
                    ( (xc) >= ' ' && (xc) <= '~' && \
                      (xc) != '\\' && (xc) != '"' \
                    )

	case T_PRT:
	    printf("outcell(%d)\n", n->offset);
	    break;

	case T_CHR:
	    if (!okay_for_cstr(n->count) ||
		    !n->next || n->next->type != T_CHR) {

		printf("outchar(%d)\n", n->count);
	    } else {
		unsigned i = 0, j;
		struct bfi * v = n;
		char *s, *p;
		while(v->next && v->next->type == T_CHR &&
			    okay_for_cstr(v->next->count)) {
		    v = v->next;
		    i++;
		}
		p = s = malloc(i+2);

		for(j=0; j<i; j++) {
		    *p++ = (char) /*GCC -Wconversion*/ n->count;
		    n = n->next;
		}
		*p++ = (char) /*GCC -Wconversion*/ n->count;
		*p++ = 0;

		printf("outstr(\"%s\")\n", s);
		free(s);
	    }
	    break;
#undef okay_for_cstr

	case T_INP:
	    printf("inpchar(%d)\n", n->offset);
	    break;

	case T_IF:
	    printf("if_start(%d,%d)\n", n->offset, n->count);
	    break;

	case T_ENDIF:
	    printf("if_end(%d)\n", n->jmp->count);
	    break;

	case T_MULT: case T_CMULT:
	case T_WHL:
	    printf("lp_start(%d,%d,%s)\n",
		    n->offset, n->count, tokennames[n->type]);
	    break;

	case T_END:
	    printf("lp_end(%d,%d)\n",
		    n->jmp->offset, n->jmp->count);
	    break;

	case T_STOP:
	    printf("bf_err()\n");
	    break;

	case T_DUMP:
	    printf("bf_dump(%d,%d)\n", n->line, n->col);
	    break;

	case T_NOP:
	    fprintf(stderr, "Warning on code generation: "
	           "%s node: ptr+%d, cnt=%d, @(%d,%d).\n",
		    tokennames[n->type],
		    n->offset, n->count, n->line, n->col);
	    break;

	default:
	    printf("%s\t%d,%d,%d,%d,%d,%d\n",
		tokennames[n->type],
		    n->offset, n->count,
		    n->offset2, n->count2,
		    n->offset3, n->count3);
	    fprintf(stderr, "Error on code generation:\n"
	           "Bad node: type %d: ptr+%d, cnt=%d.\n",
		    n->type, n->offset, n->count);
	    break;
	}
	n=n->next;
    }
    printf("bf_end()\n");
}

int
getch(int oldch)
{
    int c = EOF;
    /* The "standard" getwchar() is really dumb, it will refuse to read
     * characters from a stream if getchar() has touched it first.
     * It does this EVEN if this is an ASCII+Unicode machine.
     *
     * So I have to mess around with this...
     *
     * But it does give me somewhere to stick the EOF rubbish.
     */
    pause_runclock();
    if (input_string) {
	char * p = 0;
	c = (unsigned char)*input_string;
	if (c == 0) c = EOF;
	else
	    p = strdup(input_string+1);
	free(input_string);
	input_string = p;
    }
    else for(;;) {
	if (enable_trace || debug_mode) fflush(stdout);
#ifdef _WIN32
	fflush(stdout);
#endif
	if (iostyle == 3) {
	    int rv;
	    rv = scanf("%d", &c);
	    if (rv == EOF || rv == 0)
		c = EOF;
	    else
		c = UM(c);
	} else
#ifdef __STDC_ISO_10646__
	if (iostyle == 1) {
	    int rv;
	    wchar_t wch = EOF;
	    rv = scanf("%lc", &wch);
	    if (rv == EOF)
		c = EOF;
	    else if (rv == 0)
		continue;
	    else
		c = wch;
	} else
#endif
	    c = getchar();

	if (iostyle < 2 && c == '\r') continue;
	break;
    }
    unpause_runclock();

    if (c != EOF) return c;
    switch(eofcell)
    {
    case 2: return -1;
    case 3: return 0;
    case 4: return EOF;
    default: return oldch;
    }
}

void
putch(int ch)
{
    pause_runclock();
#ifndef _WIN32
#ifdef __STDC_ISO_10646__
    if (iostyle == 1 && cell_mask>0 &&
       (cell_size > 21 || (cell_size == 21 && SM(ch) >= -128)))
	ch = SM(ch);
    else
#endif
	ch = UM(ch);
    if (iostyle == 3) {
	printf("%d\n", ch);
    } else
#ifdef __STDC_ISO_10646__
    if (ch > 127 && iostyle == 1)
	printf("%lc", ch);
    else
#endif
	putchar(ch);
#else
    if (iostyle == 3) {
	printf("%d\n", ch);
    } else if (iostyle == 2 || iostyle == 0) {
	putchar(ch & 0xFF);
    } else {
	if (cell_mask>0 && cell_size > 21)
	    oututf8(SM(ch));
	else
	    oututf8(UM(ch));
    }
#endif

    if (only_uses_putch) only_uses_putch = 2-(ch == '\n' || iostyle == 3);

    unpause_runclock();
}

void
set_cell_size(int cell_bits)
{
    /* First try for an, oversized, exact C cell type */
#if !defined(NO_EXT_BE) && defined(UINT64_MAX)
    if (cell_bits == (int)sizeof(uint64_t)*CHAR_BIT) {
	cell_type = "uint64_t";
	cell_type_iso = 1;
    } else
#endif
#if !defined(NO_EXT_BE) && defined(UINTMAX_MAX)
    if (cell_bits == (int)sizeof(uintmax_t)*CHAR_BIT) {
	cell_type = "uintmax_t";
	cell_type_iso = 1;
    } else
#endif
#if !defined(NO_EXT_BE)
    if (cell_bits == (int)sizeof(long)*CHAR_BIT && sizeof(long) > sizeof(int)) {
	cell_type = "unsigned long";
    } else
#endif
#if !defined(NO_EXT_BE) && defined(__SIZEOF_INT128__)
    if (cell_bits == (int)sizeof(__int128)*CHAR_BIT) {
	cell_type = "unsigned __int128";
    } else
#endif
	cell_type = 0;

#ifdef BE_DC
    if (cell_bits > (int)sizeof(int)*CHAR_BIT && cell_type == 0) {
	cell_type = "C";
    }
#endif

    if (cell_type != 0) {
	cell_length = cell_bits;
	cell_size = 0;
	cell_mask = ~0;
	cell_smask = 0;

	if (verbose>5) fprintf(stderr, "Cell type is %s\n", cell_type);
	return;
    }

    if (cell_bits >= (int)sizeof(int)*CHAR_BIT || cell_bits <= 0) {
	if (cell_bits == -1 && opt_bytedefault) {
	    cell_size = 8;	/* BF definition not CHAR_BIT */
	    cell_mask = (2 << (cell_size-1)) - 1;	/* GCC Whinge */
	} else {
	    cell_size = (int)sizeof(int)*CHAR_BIT;
	    cell_mask = ~0;
	}
    } else {
	cell_size = cell_bits;
	cell_mask = (2 << (cell_size-1)) - 1;	/* GCC Whinge */
    }

    cell_length = cell_size;
    cell_smask = (1U << (cell_size-1));
    if (cell_smask < 127) cell_smask = 0;

    if (verbose>5) {
	fprintf(stderr,
		"Cell size=%d, cell_length=%d, mask = 0x%x/0x%x\n",
		cell_size, cell_length, cell_mask, cell_smask);
    }

    if (cell_size >= 0 && cell_size < 7 && iostyle != 3) {
	fprintf(stderr, "A minimum cell size of 7 bits is needed for ASCII. "
			"Add -fintio first.\n");
	exit(1);
    } else if (cell_size <= CHAR_BIT)
	cell_type = "unsigned char";
    else if (cell_mask > 0 && cell_mask <= USHRT_MAX)
	cell_type = "unsigned short";
    else if (cell_bits > (int)sizeof(int)*CHAR_BIT) {
	UsageInt64();
    } else
	cell_type = "unsigned int";
}

#ifdef _WIN32
#ifndef ENABLE_WRAP_AT_EOL_OUTPUT
#define ENABLE_WRAP_AT_EOL_OUTPUT 2
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 4
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 8
#endif

int
SetupVT100Console(int l_iostyle)
{
    // Set output mode to handle virtual terminal sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0, dwDelta;

    if (l_iostyle > 2) { iostyle = l_iostyle; return; }
    iostyle = 2;

    if (hOut == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (!GetConsoleMode(hOut, &dwMode))
        return GetLastError();

    dwDelta = ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (l_iostyle == 2)
	dwMode &= ~dwDelta;
    else
	dwMode |= dwDelta;

    if (!SetConsoleMode(hOut, dwMode))
        return GetLastError();

    switch(l_iostyle) {
    case 2:
	if (!SetConsoleOutputCP(GetOEMCP()))
	    return GetLastError();
	if (!SetConsoleCP(GetOEMCP()))
	    return GetLastError();
	break;
    case 1:
	if (!SetConsoleOutputCP(65001))
	    return GetLastError();
	if (!SetConsoleCP(65001))
	    return GetLastError();
	break;
    case 0:
	if (!SetConsoleOutputCP(GetACP()))
	    return GetLastError();
	if (!SetConsoleCP(GetACP()))
	    return GetLastError();
	break;
    }

    iostyle = l_iostyle;

    return 0;
}

/* Output a UTF-8 codepoint. */
void
oututf8(int input_chr)
{
    if (input_chr < 0x80) {            /* one-byte character */
	putchar(input_chr);
    } else if (input_chr < 0x800) {    /* two-byte character */
	putchar(0xC0 | (0x1F & (input_chr >>  6)));
	putchar(0x80 | (0x3F & (input_chr      )));
    } else if (input_chr < 0x10000) {  /* three-byte character */
	putchar(0xE0 | (0x0F & (input_chr >> 12)));
	putchar(0x80 | (0x3F & (input_chr >>  6)));
	putchar(0x80 | (0x3F & (input_chr      )));
    } else if (input_chr < 0x200000) { /* four-byte character */
	putchar(0xF0 | (0x07 & (input_chr >> 18)));
	putchar(0x80 | (0x3F & (input_chr >> 12)));
	putchar(0x80 | (0x3F & (input_chr >>  6)));
	putchar(0x80 | (0x3F & (input_chr      )));
    } else if (input_chr < 0x4000000) {/* five-byte character */
	putchar(0xF8 | (0x03 & (input_chr >> 24)));
	putchar(0x80 | (0x3F & (input_chr >> 18)));
	putchar(0x80 | (0x3F & (input_chr >> 12)));
	putchar(0x80 | (0x3F & (input_chr >>  6)));
	putchar(0x80 | (0x3F & (input_chr      )));
    } else {                           /* six-byte character */
	putchar(0xFC | (0x01 & (input_chr >> 30)));
	putchar(0x80 | (0x3F & (input_chr >> 24)));
	putchar(0x80 | (0x3F & (input_chr >> 18)));
	putchar(0x80 | (0x3F & (input_chr >> 12)));
	putchar(0x80 | (0x3F & (input_chr >>  6)));
	putchar(0x80 | (0x3F & (input_chr      )));
    }
}
#endif
