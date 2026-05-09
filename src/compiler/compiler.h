#ifndef BCAUSE_COMPILER_H
#define BCAUSE_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "list.h"

/* Default output names used when the user does not provide an explicit -o. */
#define A_OUT            "a.out"
#define A_S              "a.s"
#define A_O              "a.o"

/* ANSI escape sequences used by diagnostic printing helpers. */
#define COLOR_RESET      "\033[0m"
#define COLOR_BOLD_RED   "\033[1m\033[31m"
#define COLOR_BOLD_WHITE "\033[1m\033[37m"

/* Print a quoted token in highlighted form inside an error message. */
#define QUOTE_FMT(str)   COLOR_BOLD_WHITE "‘" str "’" COLOR_RESET

/* BCause targets x86_64, so B's single word-sized type is intptr_t-sized. */
#define X86_64_WORD_SIZE sizeof(intptr_t)

/* Current source position used by diagnostics while parsing a source file. */
struct compiler_pos {
	// TODO: 'whitespace' skips line before checking for semicolon,
	// so displaying errors expecting semicolons may show wrong line.
	char const *file_name;
	size_t      line;
};

/*
 * Complete mutable compiler state.
 *
 * This structure deliberately mixes command-line settings, current parser
 * position, per-function symbol tables, and the current string literal table.
 * The compiler is small and single-pass, so this object is threaded through
 * parsing and code-generation instead of using separate frontend/backend state.
 */
struct compiler_args {
	char const         *arg0;            /* name of the executable */
	char               *lib_dir;         /* location of B library */
	char               *output_file;     /* output file */
	char              **input_files;     /* input files */
	int                 num_input_files; /* number of input files */

	unsigned char       word_size;       /* size of the B data type */

	bool                do_linking;      /* should the compiler link? */
	bool                do_assembling;   /* should the compiler assemble? */
	bool                save_temps;      /* should temporary files get deleted? */

	struct compiler_pos pos;             /* current position in the source code */

	struct list         locals;          /* local variables in the current function */
	uintmax_t           stack_offset;    /* next local stack slot, counted in B words */
	struct list         extrns;          /* extrn variables visible in the current function */

	struct list         strings;         /* string literals waiting to be emitted to .rodata */
};

#ifdef __GNUC__
__attribute((format(printf, 2, 3)))
#endif
/* Print a compiler-level error message prefixed by argv[0]. */
void eprintf(const char *arg0, const char *fmt, ...);

/* Run the full compilation pipeline described by args. */
int  compile(struct compiler_args *args);

#endif
