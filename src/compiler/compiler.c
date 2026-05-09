#include "compiler.h"
#include "list.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

/*
 * File map:
 * - driver: compile(), subprocess()
 * - scanner: comment(), whitespace(), identifier(), number()
 * - literals/data: character(), string(), ival(), global(), vector(), strings()
 * - expressions: term(), postfix(), binary_expr(), cmp_expr(), assign_expr(), expression()
 * - statements/functions: statement(), arguments(), function(), declarations()
 */

/*
 * Expression precedence levels used by expression():
 *   2 unary/postfix handled in term()/postfix()
 *   3 * / %
 *   4 + -
 *   5 << >>
 *   6 < <= > >=
 *   7 == !=
 *   8 &
 *  10 |
 *  13 ?:
 *  14 assignment
 *  15 full expression
 */

/*
 * Read one character and require it to be exactly `expect`.
 *
 * This is a parser convenience macro, not recovery logic: mismatch is fatal.
 * The variadic arguments are forwarded to eprintf(), so callers usually spell
 * the local grammar expectation at the call site.
 */
#define ASSERT_CHAR(args, in, expect, ...)    \
	do {                                      \
		char _c;                              \
		if ((_c = fgetc(in)) != expect) {     \
			eprintf(args->arg0, __VA_ARGS__); \
			exit(1);                          \
		}                                     \
	}                                         \
	while (0)

/* BCause follows the first six integer/pointer argument registers of SysV ABI. */
#define MAX_FN_CALL_ARGS 6

/* Register order used both when receiving function parameters and making calls. */
static char const *arg_registers [MAX_FN_CALL_ARGS] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

/* Comparison operators are lowered through `cmp` plus one x86_64 setcc opcode. */
enum cmp_operator
{
	CMP_LT = 0, /* less-than operator */
	CMP_LE,     /* less-than-equal operator */
	CMP_GT,     /* greater-than operator */
	CMP_GE,     /* greater-than-equal operator */
	CMP_EQ,     /* equality operator */
	CMP_NE,     /* non-equality operator */
};

/* Indexed by enum cmp_operator; emits one byte boolean result into %al. */
char const *cmp_instruction [CMP_NE + 1] = {
  "setl",
  "setle",
  "setg",
  "setge",
  "sete",
  "setne",
};

/* Binary operators whose assembly templates are simple enough to table-drive. */
enum binary_operator
{
	/* + */ BIN_ADD = 0,
	/* - */ BIN_SUB,
	/* * */ BIN_MUL,
	/* / */ BIN_DIV,
	/* % */ BIN_MOD,
	/* << */ BIN_SHL,
	/* >> */ BIN_SAR,
	/* & */ BIN_AND,
	/* | */ BIN_OR,
};

/*
 * Assembly snippets for binary operators.
 *
 * Convention before each snippet:
 * - left operand has already been pushed on the stack;
 * - right operand is in %rax;
 * - snippet must leave the result in %rax.
 */
char const* binary_code[BIN_OR + 1] = {
    /* + */     "  pop %rdi\n"
                "  add %rdi, %rax\n",

    /* - */     "  mov %rax, %rdi\n"
                "  pop %rax\n"
                "  sub %rdi, %rax\n",

    /* * */     "  pop %rdi\n"
                "  imul %rdi, %rax\n",

    /* / */     "  mov %rax, %rdi\n"
                "  pop %rax\n"
                "  cqo\n"
                "  idiv %rdi\n",

    /* % */     "  mov %rax, %rdi\n"
                "  pop %rax\n"
                "  cqo\n"
                "  idiv %rdi\n"
                "  mov %rdx, %rax\n",

    /* << */    "  mov %rax, %rcx\n"
                "  pop %rax\n"
                "  shl %cl, %rax\n",

    /* >> */    "  mov %rax, %rcx\n"
                "  pop %rax\n"
                "  sar %cl, %rax\n",

    /* & */     "  pop %rdi\n"
                "  and %rdi, %rax\n",

    /* | */     "  pop %rdi\n"
                "  or %rdi, %rax\n",
};

/* One local symbol in the current function's stack frame. */
struct stack_var {
	char         *name;   /* local identifier, owned by this object */
	unsigned long offset; /* word offset in the compiler's local-slot model */
};

//
// Allocate structure for a stack variable.
//
static struct stack_var *init_stack_var(char const *name, unsigned long offset) {
	/* Allocate metadata for one local symbol. */
	struct stack_var *ptr = ( struct stack_var * ) malloc(sizeof(struct stack_var));

	/* Copy the identifier because parser buffers are reused aggressively. */
	ptr->name             = strdup(name);

	/* Store the logical word offset; byte conversion happens at codegen sites. */
	ptr->offset           = offset;
	return ptr;
}

//
// Deallocate a stack variable structure.
//
static void free_stack_var(struct stack_var *ptr) {
	/* `name` was duplicated in init_stack_var(). */
	free(ptr->name);

	/* The metadata node itself is owned by the locals list user. */
	free(ptr);
}

/* Forward declarations break the recursive parser dependency graph. */
static void expression(struct compiler_args *args, FILE *in, FILE *out, int level);
static void declarations(struct compiler_args *args, FILE *in, FILE *buffer);
static int  subprocess(char const *arg0, char const *p_name, char *const *p_arg);

//
// Print message with prefix "error:".
//
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
void eprintf(const char *arg0, const char *fmt, ...) {
	/* Format errors like a compiler driver: `<program>: error: ...`. */
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, COLOR_BOLD_WHITE "%s: " COLOR_BOLD_RED "error: " COLOR_RESET, arg0);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

//
// Print message with source position
//
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
void eprintf_pos(const struct compiler_pos *pos, const char *fmt, ...) {
	/* Parser errors add current source filename and line number. */
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, COLOR_BOLD_WHITE "%s:%zu: " COLOR_BOLD_RED "error: " COLOR_RESET, pos->file_name, pos->line);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

//
// Concatenate two strings into a dynamically allocated buffer.
//
char *concat(char const *a, char const *b) {
	/* +1 reserves the terminating NUL. */
	unsigned len    = strlen(a) + strlen(b) + 1;

	/* calloc gives a zeroed buffer, though strcpy/strcat overwrite all content. */
	char    *result = calloc(1, len);
	if (!result) {
		fprintf(stderr, "out of memory in concat()\n");
		exit(1);
	}

	/* Build `a` followed by `b`; caller owns the returned string. */
	strcpy(result, a);
	strcat(result, b);
	return result;
}

//
// Run compiler with given arguments.
//
int compile(struct compiler_args *args) {
	/* Assembly is first generated into a memory stream, then written to disk. */
	char  *buf;

	/* If assembling is enabled, the assembly file is a temporary beside output. */
	char  *asm_file = args->do_assembling ? concat(args->output_file, ".s") : args->output_file;

	/* If linking is enabled, the object file is also an intermediate product. */
	char  *obj_file = args->do_linking ? concat(args->output_file, ".o") : args->output_file;

	/* buf_len is filled by open_memstream() when the stream is flushed/closed. */
	size_t buf_len, len, i;

	/* GNU extension: gives codegen a FILE* while storing bytes in memory. */
	FILE  *buffer = open_memstream(&buf, &buf_len);

	/* `out` is the physical .s file; `in` is each source file. */
	FILE  *out, *in;

	/* Exit status from external assembler/linker subprocesses. */
	int    exit_code;

	/* Open every provided `.b` file and append its generated assembly. */
	for (i = 0; i < ( size_t ) args->num_input_files; i++) {
		/* Extension test below needs the filename length. */
		len = strlen(args->input_files [i]);

		/* Reject missing paths and directories before trying to parse. */
		struct stat sbuf;
		if (stat(args->input_files [i], &sbuf) != 0 || S_ISDIR(sbuf.st_mode)) {
			eprintf(args->arg0, "cannot open file " QUOTE_FMT("%s") ".\n", args->input_files [i]);
			return 1;
		}

		/* Only files ending in `.b` are treated as B source files. */
		if (len >= 2 && args->input_files [i][len - 1] == 'b' && args->input_files [i][len - 2] == '.') {
			/* Reset diagnostic position for this translation input. */
			args->pos.file_name = args->input_files [i];
			args->pos.line      = 1;

			/* Open source text for the character-level parser. */
			if (!(in = fopen(args->input_files [i], "r"))) {
				eprintf(args->arg0, "%s: %s\ncompilation terminated.\n", args->input_files [i], strerror(errno));
				return 1;
			}

			/* Top-level declarations drive parsing and emit assembly into buffer. */
			declarations(args, in, buffer);

			/* This input file is fully consumed; move to the next one. */
			fclose(in);
		}
	}

	/* Closing the memory stream finalizes `buf` and `buf_len`. */
	fclose(buffer);

	/* Materialize the generated assembly as a real .s file for GNU as. */
	if (!(out = fopen(asm_file, "w"))) {
		eprintf(args->arg0, "cannot open file " QUOTE_FMT("%s") " %s.\n", A_S, strerror(errno));
		return 1;
	}

	/* Write the complete generated assembly in one shot. */
	fwrite(buf, buf_len, 1, out);
	fclose(out);

	/* `buf` was allocated by open_memstream(). */
	free(buf);

	/* Optional stage: assemble `.s` into an object file. */
	if (args->do_assembling) {
		/* Use the system assembler rather than implementing object emission. */
		if ((exit_code = subprocess(args->arg0, "as", (char *const []) {"as", asm_file, "-o", obj_file, 0}))) {
			eprintf(args->arg0, "error running assembler (exit code %d)\n", exit_code);
			return 1;
		}

		/* Unless requested for inspection, the assembly file is temporary. */
		if (!args->save_temps) remove(asm_file);
	}

	/* Optional stage: link the object with libb.a into a static executable. */
	if (args->do_linking) {
		/*
		 * -static/-nostdlib make libb's `_start` and syscalls the runtime base.
		 * args->lib_dir usually supplies the build/test directory containing libb.a.
		 */
		if ((exit_code = subprocess(
			   args->arg0, "ld",
			   (char *const []) {
				 "ld", "-static", "-nostdlib", obj_file, args->lib_dir, "-L/lib64", "-L/usr/local/lib", "-lb", "-o",
				 args->output_file, "-z", "noexecstack", 0}
			 )))
		{
			eprintf(args->arg0, "error running linker (exit code %d)\n", exit_code);
			return 1;
		}

		/* The object file is also temporary in full compile-and-link mode. */
		if (!args->save_temps) remove(obj_file);
	}

	/* Reaching here means every requested pipeline stage succeeded. */
	return 0;
}

//
// Execute a program as a sub-process.
// Wait for completion.
// Return error status.
//
static int subprocess(char const *arg0, char const *p_name, char *const *p_arg) {
	/* Echo the external command, which makes test logs and manual runs inspectable. */
	fprintf(stdout, "%s", p_name);
	for (unsigned i = 1; p_arg [i]; i++) fprintf(stdout, " %s", p_arg [i]);
	fprintf(stdout, "\n");
	fflush(stdout);

	/* Fork so the child can become the requested external program. */
	pid_t pid = fork();

	/* Negative pid means fork itself failed; this is a driver-level fatal error. */
	if (pid < 0) {
		eprintf(arg0, "error forking parent process " QUOTE_FMT("%s") "\n", arg0);
		exit(1);
	}

	/* Child path: replace this process image with `as` or `ld`. */
	if (pid == 0 && execvp(p_name, p_arg) == -1) {
		eprintf(arg0, "error executing " QUOTE_FMT("%s") ": %s\n", p_name, strerror(errno));
		exit(1);
	}

	/* Parent path: wait synchronously for the child tool to finish. */
	int pid_status;
	if (waitpid(pid, &pid_status, 0) == -1) {
		eprintf(arg0, "error getting status of child process %d\n", pid);
		exit(1);
	}

	/* Return only the child exit code; callers decide how to report failure. */
	return WEXITSTATUS(pid_status);
}

//
// Parse a comment.
// It starts with /* and finishes with */.
//
static void comment(struct compiler_args *args, FILE *in) {
	int c;

	/* Consume until the first closing delimiter, updating line diagnostics. */
	while ((c = fgetc(in)) != EOF) {
		if (c == '\n') ++args->pos.line;
		/* Only '*' can begin the closing delimiter. */
		if (c == '*') {
			if ((c = fgetc(in)) == '/') return;
			/* Not a delimiter: put the lookahead back. */
			ungetc(c, in);
		}
	}

	eprintf_pos(&args->pos, "unclosed comment, expect " QUOTE_FMT("*/") " to close the comment\n");
	exit(1);
}

//
// Skip whitespace characters and comments.
//
static void whitespace(struct compiler_args *args, FILE *in) {
	( void ) args;
	int c;

	/* Skip whitespace and keep line number state. */
	while ((c = fgetc(in)) != EOF) {
		if (isspace(c)) {
			if (c == '\n') ++args->pos.line;
			continue;
		}

		/* B comments are C-style block comments. Slash is otherwise meaningful. */
		if (c == '/') {
			if ((c = fgetc(in)) == '*') {
				comment(args, in);
				continue;
			}
			else {
				/* Not a comment: restore both characters for the expression parser. */
				ungetc(c, in);
				ungetc('/', in);
				return;
			}
		}
		/* First non-space token byte belongs to the caller. */
		ungetc(c, in);
		return;
	}
}

//
// Parse an identifier.
// It may include alphanumeric characters or underscore.
//
static int identifier(struct compiler_args *args, FILE *in, char *buffer) {
	int read = 0;
	int c;

	/* Identifiers are tokenized only after leading trivia is removed. */
	whitespace(args, in);

	while ((c = fgetc(in)) != EOF) {
		/* The first non-identifier byte terminates the token and is preserved. */
		if (!isalpha(c) && !isalnum(c) && c != '_') {
			ungetc(c, in);
			buffer [read] = '\0';
			return read;
		}
		/* Caller supplies storage for the identifier spelling. */
		buffer [read++] = c;
	}
	/* EOF can terminate a final identifier just like punctuation can. */
	buffer [read] = '\0';
	return read;
}

//
// Parse an integer literal, possibly empty.
// Leading zero means octal value.
//
static intptr_t number(struct compiler_args *args, FILE *in) {
	intptr_t num = 0;
	int      c, base;

	/* Integer literals may appear after arbitrary whitespace/comments. */
	whitespace(args, in);
	c = fgetc(in);
	if (c == EOF) return EOF;
	/* Historical B/C rule: leading zero selects octal. */
	if (c == '0') base = 8;
	else base = 10;
	while (isdigit(c)) {
		num = (num * base) + c - '0';
		c   = fgetc(in);
		if (c == EOF) return EOF;
	}
	/* Return the delimiter to the parser that knows the surrounding grammar. */
	ungetc(c, in);
	return num;
}

//
// Parse a multi-character literal.
// Return value.
//
static intptr_t character(struct compiler_args *args, FILE *in) {
	int      c = 0;
	int      i;
	intptr_t value = 0;

	/* A B character constant can pack multiple bytes into one machine word. */
	for (i = 0; i < args->word_size; i++) {
		/* Closing quote before word_size bytes simply ends the literal. */
		if ((c = fgetc(in)) == '\'') return value;

		/* B uses `*` as the escape introducer, not backslash. */
		if (c == '*') {
			switch (c = fgetc(in)) {
			case '0' :
			case 'e'  : c = '\0'; break;
			case '('  :
			case ')'  :
			case '*'  :
			case '\'' :
			case '"'  : break;
			case 't'  : c = '\t'; break;
			case 'n'  : c = '\n'; break;
			case 'r'  : c = '\r'; break;
			default   : eprintf_pos(&args->pos, "undefined escape character " QUOTE_FMT("*%c") "\n", c); exit(1);
			}
		}

		/* Pack byte i into the corresponding little-endian byte lane. */
		value |= (( uintptr_t ) ( uint8_t ) c) << (i * 8);
	}

	/* More than word_size bytes must still be closed explicitly. */
	if (fgetc(in) != '\'') {
		eprintf_pos(&args->pos, "unclosed char literal\n");
		exit(1);
	}

	return value;
}

//
// Parse a string literal.
//
static void string(struct compiler_args *args, FILE *in) {
	int    c;
	size_t alloc  = 32;
	size_t size   = 0;
	/* Strings are collected first and emitted later by strings(). */
	char  *string = ( char * ) calloc(alloc, sizeof(char));

	while ((c = fgetc(in)) != '"') {
		/* Same B escape convention as character constants. */
		if (c == '*') {
			switch (c = fgetc(in)) {
			case '0' :
			case 'e'  : c = '\0'; break;
			case '('  :
			case ')'  :
			case '*'  :
			case '\'' :
			case '"'  : break;
			case 't'  : c = '\t'; break;
			case 'n'  : c = '\n'; break;
			default   : eprintf_pos(&args->pos, "undefined escape character " QUOTE_FMT("*%c") "\n", c); exit(1);
			}
		}
		/* EOF before a closing quote cannot be recovered. */
		else if (c == EOF) {
			eprintf_pos(&args->pos, "unterminated string literal\n");
			exit(1);
		}
		/* Append one byte and grow the temporary buffer if needed. */
		string [size] = c;
		size++;
		if (size >= alloc) string = ( char * ) realloc(string, (alloc *= 2) * sizeof(char));
	}
	/* Keep C-style NUL termination for strlen() during .rodata emission. */
	string [size] = 0;
	/* Ownership moves to args->strings; strings() later frees it. */
	list_push(&args->strings, string);
}

//
// Parse one initialization value.
// It can be:
//      integer literal
//      negative integer literal
//      'char'
//      "string"
//
static void ival(struct compiler_args *args, FILE *in, FILE *out) {
	static char buffer [BUFSIZ];
	intptr_t    value;
	/* Caller has already positioned us at the first byte of the initializer. */
	int         c = fgetc(in);

	/* Symbol initializer: emit relocation against another label. */
	if (isalpha(c)) {
		ungetc(c, in);
		if (identifier(args, in, buffer) == EOF) {
			eprintf_pos(&args->pos, "unexpected end of file, expect ival\n");
			exit(1);
		}
		fprintf(out, "  .quad %s\n", buffer);
	}
	/* Character literal initializer: emit packed word value. */
	else if (c == '\'') {
		if ((value = character(args, in)) == EOF) {
			eprintf_pos(&args->pos, "unexpected end of file, expect ival\n");
			exit(1);
		}
		fprintf(out, "  .quad %lu\n", value);
	}
	/* String initializer: emit pointer to the deferred string table entry. */
	else if (c == '\"') {
		string(args, in);
		fprintf(out, "  .quad .string.%lu\n", args->strings.size - 1);
	}
	/* Negative integer is parsed as sign plus unsigned literal. */
	else if (c == '-') {
		if ((value = number(args, in)) == EOF) {
			eprintf_pos(&args->pos, "unexpected end of file, expect ival\n");
			exit(1);
		}
		fprintf(out, "  .quad -%lu\n", value);
	}
	else {
		/* Plain integer initializer. */
		ungetc(c, in);
		if ((value = number(args, in)) == EOF) {
			eprintf_pos(&args->pos, "unexpected end of file, expect ival\n");
			exit(1);
		}
		fprintf(out, "  .quad %lu\n", value);
	}
}

//
// Parse declaration of a global scalar variable.
// An optional initialization list can be present.
//
static void global(struct compiler_args *args, FILE *in, FILE *out, char *identifier) {
	/* A scalar symbol labels the first word of storage in .data. */
	fprintf(out, ".data\n" ".type %s, @object\n" ".align %d\n" "%s:\n", identifier, args->word_size, identifier);

	int c;
	/* No immediate semicolon means an initializer list follows. */
	if ((c = fgetc(in)) != ';') {
		ungetc(c, in);
		do {
			/* Each initializer emits one `.quad`. */
			whitespace(args, in);
			ival(args, in, out);
			whitespace(args, in);
		}
		while ((c = fgetc(in)) == ',');

		if (c != ';') {
			eprintf_pos(&args->pos, "expect " QUOTE_FMT(";") " at end of declaration\n");
			exit(1);
		}
	}
	/* Uninitialized scalar reserves exactly one B word. */
	else fprintf(out, "  .zero %d\n", args->word_size);
}

//
// Parse declaration of a global array.
// An optional initialization list can be present.
//
static void vector(struct compiler_args *args, FILE *in, FILE *out, char *identifier) {
	intptr_t nwords = 0;
	int      c;

	/* Empty brackets mean no declared element count. */
	whitespace(args, in);
	if ((c = fgetc(in)) != ']') {
		ungetc(c, in);
		/* Non-empty brackets carry the number of element words. */
		nwords = number(args, in);
		if (nwords == EOF) {
			eprintf_pos(&args->pos, "unexpected end of file, expect vector size after " QUOTE_FMT("[") "\n");
			exit(1);
		}
		whitespace(args, in);

		if (fgetc(in) != ']') {
			eprintf_pos(&args->pos, "expect " QUOTE_FMT("]") " after vector size\n");
			exit(1);
		}
	}

	/*
	 * A global vector symbol stores a pointer to its first element.
	 * The first data element starts immediately after that pointer word.
	 */
	fprintf(out,
        ".data\n.type %s, @object\n"
        ".align %d\n"
        "%s:\n"
        "  .quad .+8\n",
        identifier, args->word_size, identifier
    );

	whitespace(args, in);

	/* Optional initializer list fills element words after the pointer. */
	if ((c = fgetc(in)) != ';') {
		ungetc(c, in);
		do {
			whitespace(args, in);
			ival(args, in, out);
			whitespace(args, in);
			nwords--;
		}
		while ((c = fgetc(in)) == ',');

		if (c != ';') {
			eprintf_pos(&args->pos, "expect " QUOTE_FMT(";") " at end of declaration\n");
			exit(1);
		}
	}

	/* If a declared size remains after explicit initializers, zero-fill it. */
	if (nwords > 0) fprintf(out, "  .zero %ld\n", args->word_size * nwords);
}

//
// Find given name among locals or externs of current function.
//
static intptr_t find_identifier(struct compiler_args *args, char const *buffer, bool *is_extrn) {
	size_t            i;
	struct stack_var *var;

	/* Locals win over externals: a stack variable shadows an extrn declaration. */
	for (i = 0; i < args->locals.size; i++) {
		var = ( struct stack_var * ) args->locals.data [i];
		if (strcmp(buffer, var->name) == 0) {
			/* Tell the caller this resolves to a frame-relative address. */
			if (is_extrn) *is_extrn = false;
			return var->offset;
		}
	}

	/* Externals are labels resolved later by assembler/linker. */
	for (i = 0; i < args->extrns.size; i++) {
		if (strcmp(buffer, args->extrns.data [i]) == 0) {
			/* Tell the caller this resolves to a RIP-relative symbol address. */
			if (is_extrn) *is_extrn = true;
			return i;
		}
	}

	/* Negative means not found in either table. */
	return -1;
}

//
// Parse a postfix operation.
// Return true when result is lvalue (address of the value).
//
static bool postfix(struct compiler_args *args, FILE *in, FILE *out, bool is_lvalue) {
	int c, num_args = 0;

	/* Postfix operators extend the address/value produced by term(). */
	switch (c = fgetc(in)) {
	case '[' :
		/* Indexing expects %rax to be an address of a vector variable. */
		fprintf(out, "  push (%%rax)\n");
		/* Compute the index expression; its rvalue ends in %rax. */
		expression(args, in, out, 15);
		/* B indexing is word-based: index * 8 plus the vector base pointer. */
		fprintf(out, "  pop %%rdi\n  shl $3, %%rax\n  add %%rdi, %%rax\n");

		if ((c = fgetc(in)) != ']') {
			eprintf_pos(
			  &args->pos,
			  "unexpected token " QUOTE_FMT("%c") ", expect closing " QUOTE_FMT("]") " after index expression\n", c
			);
			exit(1);
		}
		/* The result of `a[i]` is an address until a later context fetches it. */
		is_lvalue = true;
		break;

	case '(' :
		/* Function call: current %rax is the callee address. Save it first. */
		fprintf(out, "  push %%rax\n");

		/* Parse arguments left-to-right, pushing each computed rvalue. */
		while ((c = fgetc(in)) != ')') {
			ungetc(c, in);
			expression(args, in, out, 15);

			if (++num_args > MAX_FN_CALL_ARGS) {
				eprintf_pos(&args->pos, "only %d call arguments are currently supported\n", MAX_FN_CALL_ARGS);
				exit(1);
			}
			/* Temporarily stack arguments so they can be popped into ABI registers. */
			fprintf(out, "  push %%rax\n");

			whitespace(args, in);
			if ((c = fgetc(in)) == ')') break;
			else if (c == ',') continue;

			eprintf_pos(
			  &args->pos,
			  "unexpected character " QUOTE_FMT("%c") ", expect closing " QUOTE_FMT(")") " after call expression\n", c
			);
			exit(1);
		}

		/* Pop in reverse so arg0 lands in %rdi, arg1 in %rsi, etc. */
		while (num_args > 0) fprintf(out, "  pop %s\n", arg_registers [--num_args]);

		/* Restore callee address into %r10 and indirect-call it. */
		fprintf(out, "  pop %%r10\n  call *%%r10\n");
		/* Call result is a value in %rax, never an assignable address. */
		is_lvalue = false;
		break;

	case '+' :
		if ((c = fgetc(in)) != '+') {
			ungetc(c, in);
			ungetc('+', in);
			break;
		}

		/* Postfix increment returns the old value but mutates the lvalue storage. */
		fprintf(out, "  mov (%%rax), %%rcx\n" "  addq $1, (%%rax)\n" "  mov %%rcx, %%rax\n");
		is_lvalue = false;
		break;

	case '-' :
		if ((c = fgetc(in)) != '-') {
			ungetc(c, in);
			ungetc('-', in);
			break;
		}

		/* Postfix decrement mirrors increment: old value is the expression result. */
		fprintf(out, "  mov (%%rax), %%rcx\n" "  subq $1, (%%rax)\n" "  mov %%rcx, %%rax\n");
		is_lvalue = false;
		break;

	/* No postfix operator: return the byte to the surrounding expression parser. */
	default : ungetc(c, in); break;
	}
	return is_lvalue;
}

//
// Parse a term.
// It may have only unary operations (no binary ops).
// Return true when it's an lvalue (address of the value).
//
static bool term(struct compiler_args *args, FILE *in, FILE *out) {
	static char buffer [BUFSIZ];
	int         c;
	intptr_t    value;
	bool        is_lvalue = false, is_extrn = false;

	/* term() consumes leading trivia and then recognizes one expression atom. */
	whitespace(args, in);

	switch (c = fgetc(in)) {
	case '\'' : /* character literal */
		/* Literal values are immediate rvalues in %rax. */
		if ((value = character(args, in))) fprintf(out, "  mov $%lu, %%rax\n", value);
		else fprintf(out, "  xor %%rax, %%rax\n");
		break;

	case '\"' : /* string literal */
		/* String literal result is the address of a deferred .rodata entry. */
		string(args, in);
		fprintf(out, "  lea .string.%lu(%%rip), %%rax\n", args->strings.size - 1);
		break;

	case '(' : /* parentheses */
		/* Parentheses delegate to the full expression parser at maximum level. */
		expression(args, in, out, 15);
		ASSERT_CHAR(
		  args, in, ')', "expect " QUOTE_FMT(")") " after " QUOTE_FMT("(<expr>") ", got " QUOTE_FMT("%c") "\n", c
		);
		break;

	case '!' : /* not operator */
		/* Unary operators recurse into term(), then fetch if the operand is lvalue. */
		if (term(args, in, out)) {
			/* fetch rvalue */
			fprintf(out, "  mov (%%rax), %%rax\n");
		}
		/* Logical not normalizes to 0 or 1. */
		fprintf(out, "  cmp $0, %%rax\n  sete %%al\n  movzx %%al, %%rax\n");
		break;

	case '-' :
		if ((c = fgetc(in)) == '-') { /* prefix decrement operator */
			/* Prefix decrement requires an addressable operand. */
			if (!term(args, in, out)) {
				eprintf_pos(&args->pos, "expected lvalue after " QUOTE_FMT("--") "\n");
				exit(1);
			}
			/* Mutate storage; the expression remains lvalue-like in this implementation. */
			fprintf(out, "  mov (%%rax), %%rdi\n  sub $1, %%rdi\n  mov %%rdi, (%%rax)\n");
			is_lvalue = true;
		}
		else { /* negation operator */ ungetc(c, in);
			/* Arithmetic negation works on a value, not an address. */
			if (term(args, in, out)) {
				/* fetch rvalue */
				fprintf(out, "  mov (%%rax), %%rax\n");
			}
			fprintf(out, "  neg %%rax\n");
		}
		break;

	case '+' : /* prefix increment operator */
		/* A bare unary plus is not supported here; only `++x`. */
		if ((c = fgetc(in)) != '+') {
			eprintf_pos(&args->pos, "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT("+") "\n", c);
			exit(1);
		}
		/* Prefix increment requires storage to mutate. */
		if (!term(args, in, out)) {
			eprintf_pos(&args->pos, "expected lvalue after " QUOTE_FMT("++") "\n");
			exit(1);
		}
		/* Store incremented value back through the address in %rax. */
		fprintf(out, "  mov (%%rax), %%rdi\n  add $1, %%rdi\n  mov %%rdi, (%%rax)\n");
		is_lvalue = true;
		break;

	case '*' : /* indirection operator */
		/* Dereference first obtains a pointer value, then treats it as an address. */
		if (term(args, in, out)) {
			/* fetch rvalue */
			fprintf(out, "  mov (%%rax), %%rax\n");
		}
		is_lvalue = true;
		break;

	case '&' : /* address operator */
		/* Address-of is only valid on something that already produced an address. */
		if (!term(args, in, out)) {
			eprintf_pos(&args->pos, "expected lvalue after " QUOTE_FMT("&") "\n");
			exit(1);
		}
		break;

	case EOF : eprintf_pos(&args->pos, "unexpected end of file, expect expression\n"); exit(1);

	default :
		if (isdigit(c)) { /* integer literal */
			/* Numeric literal is an immediate rvalue. */
			ungetc(c, in);
			if ((value = number(args, in))) fprintf(out, "  mov $%lu, %%rax\n", value);
			else fprintf(out, "  xor %%rax, %%rax\n");
		}
		else if (isalpha(c)) { /* identifier */ is_lvalue = true;

			/* Identifiers start as lvalues: codegen first computes their address. */
			ungetc(c, in);
			identifier(args, in, buffer);

			if ((value = find_identifier(args, buffer, &is_extrn)) < 0) {

				/* Unknown identifier may still be an implicit external function call. */
				whitespace(args, in);
				c = fgetc(in);
				if (c == '(') {
					/* When next symbol is '(', add this name to the list of externals. */
					ungetc(c, in);
					list_push(&args->extrns, strdup(buffer));
					is_extrn = true;
				}
				else {
					eprintf_pos(&args->pos, "undefined identifier " QUOTE_FMT("%s") "\n", buffer);
					exit(1);
				}
			}

			/* External labels are addressed RIP-relatively; locals are frame-relative. */
			if (is_extrn) fprintf(out, "  lea %s(%%rip), %%rax\n", buffer);
			else fprintf(out, "  lea -%lu(%%rbp), %%rax\n", (value + 2) * args->word_size);

			/* Calls, indexing, and postfix inc/dec may transform the lvalue status. */
			is_lvalue = postfix(args, in, out, is_lvalue);
		}
		else {
			eprintf_pos(&args->pos, "unexpected character " QUOTE_FMT("%c") ", expect expression\n", c);
			exit(1);
		}
	}

	return is_lvalue;
}

//
// Generate code for binary operation.
//
static void binary_expr(struct compiler_args *args, FILE *in, FILE *out, enum binary_operator op, int level) {
	/* Preserve the left operand while recursively parsing the right operand. */
	fprintf(out, "  push %%rax\n");
	/* The recursive call leaves the right operand in %rax. */
	expression(args, in, out, level);
	/* Emit the operator-specific instruction sequence. */
	fputs(binary_code [op], out);
}

//
// Generate code for comparison operation.
//
static void cmp_expr(struct compiler_args *args, FILE *in, FILE *out, enum cmp_operator op, int level) {
	/* Comparison needs both operands alive, so save the left side first. */
	fprintf(out, "  push %%rax\n");
	/* Parse the right side at the same precedence level. */
	expression(args, in, out, level);
	/* Compare and normalize the result to 0 or 1 in %rax. */
	fprintf(out, "  pop %%rdi\n" "  cmp %%rax, %%rdi\n" "  %s %%al\n" "  movzb %%al, %%rax\n", cmp_instruction [op]);
}

//
// Generate code for assignment operation:
//      =+
//      =-
//      =*
//      =/
//      =%
//      =<<
//      =<=
//      =<
//      =>>
//      =>=
//      =>
//      =!=
//      ===
//      =&
//      =|
//
static void assign_expr(struct compiler_args *args, FILE *in, FILE *out, char c, int level) {
	/* Compound assignment is just a binary operator glued onto the assignment grammar. */
	switch (c) {
	case '+' : /* addition operator */ binary_expr(args, in, out, BIN_ADD, level); break;

	case '*' : /* multiplication operator */ binary_expr(args, in, out, BIN_MUL, level); break;

	case '-' : /* subtraction operator */ binary_expr(args, in, out, BIN_SUB, level); break;

	case '/' : /* division operator */ binary_expr(args, in, out, BIN_DIV, level); break;

	case '%' : /* modulo operator */ binary_expr(args, in, out, BIN_MOD, level); break;

	case '<' :
		switch (c = fgetc(in)) {
		case '<' : /* shift-left operator */ binary_expr(args, in, out, BIN_SHL, level); break;
		case '=' : /* less-than-or-equal operator */ cmp_expr(args, in, out, CMP_LE, level); break;
		default  : /* less-than operator */ ungetc(c, in); cmp_expr(args, in, out, CMP_LT, level);
		}
		break;

	case '>' :
		switch (c = fgetc(in)) {
		case '>' : /* shift-right-operator */ binary_expr(args, in, out, BIN_SAR, level); break;
		case '=' : /* greater-than-or-equal operator */ cmp_expr(args, in, out, CMP_GE, level); break;
		default  : /* greater-than operator */ ungetc(c, in); cmp_expr(args, in, out, CMP_GT, level);
		}
		break;

	case '!' : /* inequality operator */
		if ((c = fgetc(in)) != '=') {
			eprintf_pos(&args->pos, "unknown operator " QUOTE_FMT("!%c") "\n", c);
			exit(1);
		}
		cmp_expr(args, in, out, CMP_NE, level);
		break;

	case '=' : /* equality operator */
		if ((c = fgetc(in)) != '=') {
			eprintf_pos(&args->pos, "unknown operator " QUOTE_FMT("=%c") "\n", c);
			exit(1);
		}
		cmp_expr(args, in, out, CMP_EQ, level);
		break;

	case '&' : /* bitwise and operator */ binary_expr(args, in, out, BIN_AND, level); break;

	case '|' : /* bitwise or operator */ binary_expr(args, in, out, BIN_OR, level); break;

	default  : /* plain assignment */ ungetc(c, in); expression(args, in, out, level);
	}
}

//
// Parse expression.
// Allow operations up to the given precedence level.
//
static void expression(struct compiler_args *args, FILE *in, FILE *out, int level) {
	/* Start with one atom; `left_is_lvalue` tracks whether %rax currently holds an address. */
	bool          left_is_lvalue = term(args, in, out);
	int           c, c2;
	/* Unique labels for ternary operators are generated sequentially. */
	static size_t conditional = 0;

	/* Repeatedly absorb operators in descending precedence order. */
	for (;;) {
		whitespace(args, in);
		c = fgetc(in);

		if (level >= 13 && c == '?') {
			/* Ternary is the lowest-precedence operator, so resolve it at the outermost level. */
			size_t this_conditional = conditional++;

			if (left_is_lvalue) {
				/* fetch rvalue */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			/* Jump over the true branch when the condition is false. */
			fprintf(out, "  cmp $0, %%rax\n  je .L.cond.else.%ld\n", this_conditional);
			/* The true branch is parsed at slightly higher precedence to stop at ':'. */
			expression(args, in, out, 12);
			whitespace(args, in);
			if ((c2 = fgetc(in)) != ':') {
				eprintf_pos(
				  &args->pos,
				  "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT(":") " between conditional branches\n",
				  c2
				);
				exit(1);
			}
			/* False branch label, then finish at a shared end label. */
			fprintf(out, "  jmp .L.cond.end.%ld\n.L.cond.else.%ld:\n", this_conditional, this_conditional);
			expression(args, in, out, 13);
			fprintf(out, ".L.cond.end.%ld:\n", this_conditional);
			return;
		}

		//
		// Binary operations, left assosiative.
		//
		if (level >= 4 && c == '+') {
			/* addition operator */
			if (left_is_lvalue) {
				/* Convert the stored address into the value it points to. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			/* Parse the right operand at the next tighter precedence level. */
			binary_expr(args, in, out, BIN_ADD, 3);
			continue;
		}
		if (level >= 4 && c == '-') {
			/* subtraction operator */
			if (left_is_lvalue) {
				/* Like addition, subtraction works on values, not bare addresses. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_SUB, 3);
			continue;
		}
		if (level >= 3 && c == '*') {
			/* multiplication operator */
			if (left_is_lvalue) {
				/* Fetch the pointed-to value before multiplying. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_MUL, 2);
			continue;
		}
		if (level >= 3 && c == '/') {
			/* division operator */
			if (left_is_lvalue) {
				/* Division is another value-level operator. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_DIV, 2);
			continue;
		}
		if (level >= 3 && c == '%') {
			/* modulo operator */
			if (left_is_lvalue) {
				/* Modulo uses the same value-level discipline as division. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_MOD, 2);
			continue;
		}
		if (c == '<') {
			c2 = fgetc(in);
			if (level >= 5 && c2 == '<') {
				/* shift-left operator */
				if (left_is_lvalue) {
					/* Shift counts are values too; there is no address arithmetic here. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				binary_expr(args, in, out, BIN_SHL, 4);
				continue;
			}
			if (level >= 6 && c2 == '=') {
				/* less-than-or-equal operator */
				if (left_is_lvalue) {
					/* Comparison must see concrete operands. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				cmp_expr(args, in, out, CMP_LE, 5);
				continue;
			}
			ungetc(c2, in);
			if (level >= 6) {
				/* less-than operator */
				if (left_is_lvalue) {
					/* Plain < is handled exactly like <=, except for the comparison opcode. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				cmp_expr(args, in, out, CMP_LT, 5);
				continue;
			}
		}
		if (c == '>') {
			c2 = fgetc(in);
			if (level >= 5 && c2 == '>') {
				/* shift-right-operator */
				if (left_is_lvalue) {
					/* Again, convert address to value before arithmetic. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				binary_expr(args, in, out, BIN_SAR, 4);
				continue;
			}
			if (level >= 6 && c2 == '=') {
				/* greater-than-or-equal operator */
				if (left_is_lvalue) {
					/* Relational operators do not accept raw addresses. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				cmp_expr(args, in, out, CMP_GE, 5);
				continue;
			}
			ungetc(c2, in);
			if (level >= 6) {
				/* greater-than operator */
				if (left_is_lvalue) {
					/* Same shape as less-than, just a different comparison opcode. */
					fprintf(out, "  mov (%%rax), %%rax\n");
					left_is_lvalue = false;
				}
				cmp_expr(args, in, out, CMP_GT, 5);
				continue;
			}
		}
		if (level >= 7 && c == '!') {
			/* inequality operator */
			if ((c2 = fgetc(in)) != '=') {
				eprintf_pos(&args->pos, "unknown operator " QUOTE_FMT("!%c") "\n", c2);
				exit(1);
			}
			if (left_is_lvalue) {
				/* Turn the left address into a compare-ready value. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			cmp_expr(args, in, out, CMP_NE, 6);
			continue;
		}
		if (level >= 8 && c == '&') {
			/* bitwise and operator */
			if (left_is_lvalue) {
				/* Bitwise operators operate on actual values, not storage locations. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_AND, 7);
			continue;
		}
		if (level >= 10 && c == '|') {
			/* bitwise or operator */
			if (left_is_lvalue) {
				/* Bitwise OR also requires the left operand as an rvalue. */
				fprintf(out, "  mov (%%rax), %%rax\n");
				left_is_lvalue = false;
			}
			binary_expr(args, in, out, BIN_OR, 9);
			continue;
		}
		if (c == '=') {
			c2 = fgetc(in);
			if (level >= 7 && c2 == '=') {
				/* Distinguish == from assignment by looking one byte further ahead. */
				int c3 = fgetc(in);
				ungetc(c3, in);
				if (c3 != '=') {
					/* equality operator */
					if (left_is_lvalue) {
						/* Equality compares values, not addresses. */
						fprintf(out, "  mov (%%rax), %%rax\n");
						left_is_lvalue = false;
					}
					cmp_expr(args, in, out, CMP_EQ, 6);
					continue;
				}
			}
			if (level >= 14) {
				//
				// Assignment operator, right associative.
				//
				if (!left_is_lvalue) {
					eprintf_pos(&args->pos, "left operand of assignment has to be an lvalue\n");
					exit(1);
				}
				/* Save the destination address, then parse the right-hand side. */
				fprintf(out, "  push %%rax\n  mov (%%rax), %%rax\n");
				/* Compound assignment uses the same precedence level on the RHS. */
				assign_expr(args, in, out, c2, 14);
				/* Store the computed value back through the saved destination pointer. */
				fprintf(out, "  pop %%rdi\n  mov %%rax, (%%rdi)\n");
				left_is_lvalue = false;
				continue;
			}
			ungetc(c2, in);
		}

		// No more operations at this level.
		ungetc(c, in);
		if (left_is_lvalue) {
			/* If the caller expects a value, finalize the current lvalue now. */
			fprintf(out, "  mov (%%rax), %%rax\n");
		}
		/* No operator matched at this precedence level, so hand control back up. */
		return;
	}
}

//
// Parse a statement.
//
static void
statement(struct compiler_args *args, FILE *in, FILE *out, char *fn_ident, intptr_t switch_id, struct list *cases) {
	int                  c;
	static char          buffer [BUFSIZ];
	size_t               id;
	/* Every control-flow construct gets a fresh numeric suffix for labels. */
	static size_t        stmt_id         = 0;
	static unsigned long last_block_line = 1;
	intptr_t             i, value = 0;
	struct list          switch_case_list;

	/* Statements are parsed after leading trivia is removed. */
	whitespace(args, in);
	switch (c = fgetc(in)) {
	case '{' : {
		/* Save stack depth so block-local auto declarations can unwind. */
		unsigned long stack_offset = args->stack_offset;
		last_block_line            = args->pos.line;

		/* Parse statements until the closing brace or EOF. */
		whitespace(args, in);
		while ((c = fgetc(in)) != '}') {
			if (c == EOF) {
				/* If a block runs off the end of file, report the block's start line. */
				args->pos.line = last_block_line;
				eprintf_pos(&args->pos, "unexpected end of file, expect " QUOTE_FMT("}") "\n");
				exit(1);
			}
			/* Recurse into the nested statement using the same function context. */
			ungetc(c, in);
			statement(args, in, out, fn_ident, switch_id, cases);
			whitespace(args, in);
		}

		/* Restore stack depth so block-local autos do not leak past the block. */
		if (stack_offset != args->stack_offset) {
			fprintf(out, "  add $%lu, %%rsp\n", (args->stack_offset - stack_offset) * args->word_size);
			args->stack_offset = stack_offset;
		}
	} break;

	/* Null statement. */
	case ';' : break; /* null statement */

	default :
		/* Keywords, labels, and identifier-led expressions all start here. */
		if (isalpha(c)) {
			ungetc(c, in);
			identifier(args, in, buffer);
			whitespace(args, in);

			if (strcmp(buffer, "goto") == 0) { /* goto statement */
				/* goto is a direct jump to a named label in the current function. */
				if (!identifier(args, in, buffer)) {
					eprintf_pos(&args->pos, "expect label name after " QUOTE_FMT("goto") "\n");
					exit(1);
				}
				fprintf(out, "  jmp .L.label.%s.%s\n", buffer, fn_ident);
				/* goto must end immediately after the label name. */
				whitespace(args, in);
				ASSERT_CHAR(args, in, ';', "expect " QUOTE_FMT(";") " after " QUOTE_FMT("goto") " statement\n");
				return;
			}
			else if (strcmp(buffer, "return") == 0) { /* return statement */
				/* B return syntax here is either `return;` or `return(expr);`. */
				if ((c = fgetc(in)) != ';') {
					if (c != '(') {
						eprintf_pos(
						  &args->pos, "expect " QUOTE_FMT("(") " or " QUOTE_FMT(";") " after " QUOTE_FMT("return") "\n"
						);
						exit(1);
					}
					expression(args, in, out, 15);
					whitespace(args, in);
					/* The closing parenthesis and semicolon are both mandatory. */
					ASSERT_CHAR(args, in, ')', "expect " QUOTE_FMT(")") " after " QUOTE_FMT("return") " statement\n");
					whitespace(args, in);
					ASSERT_CHAR(args, in, ';', "expect " QUOTE_FMT(";") " after " QUOTE_FMT("return") " statement\n");
				}
				/* Bare return defaults to zero. */
				else fprintf(out, "  xor %%rax, %%rax\n");
				/* All returns converge on one epilogue label per function. */
				fprintf(out, "  jmp .L.return.%s\n", fn_ident);
				return;
			}
			else if (strcmp(buffer, "if") == 0) { /* conditional statement */ id = stmt_id++;

				/* One id supplies the else/end labels for this conditional. */
				ASSERT_CHAR(args, in, '(', "expect " QUOTE_FMT("(") " after " QUOTE_FMT("if") "\n");
				expression(args, in, out, 15);
				/* Zero means false; any non-zero result falls through to the then-branch. */
				fprintf(out, "  cmp $0, %%rax\n  je .L.else.%lu\n", id);
				whitespace(args, in);
				ASSERT_CHAR(args, in, ')', "expect " QUOTE_FMT(")") " after condition\n");

				/* Emit the then-branch as a nested statement. */
				statement(args, in, out, fn_ident, -1, NULL);
				/* Skip over the else-branch when the then-branch has run. */
				fprintf(out, "  jmp .L.end.%lu\n.L.else.%lu:\n", id, id);

				/* Optional else is detected by a small manual lookahead. */
				whitespace(args, in);
				memset(buffer, 0, 6 * sizeof(char));
				if ((buffer [0] = fgetc(in)) == 'e'
					&& (buffer [1] = fgetc(in)) == 'l'
					&& (buffer [2] = fgetc(in)) == 's'
					&& (buffer [3] = fgetc(in)) == 'e'
					&& !isalnum(buffer [4] = fgetc(in)))
				{
					/* Consume else as a keyword and parse the trailing statement. */
					statement(args, in, out, fn_ident, -1, NULL);
				}
				else {
					/* Put back the lookahead bytes if the token was not actually else. */
					for (i = 4; i >= 0; i--)
						if (buffer [i]) ungetc(buffer [i], in);
				}

				/* Close the if/else control-flow diamond. */
				fprintf(out, ".L.end.%lu:\n", id);
				return;
			}
			else if (strcmp(buffer, "while") == 0) { /* while statement */ id = stmt_id++;

				/* while loops get a start/end label pair. */
				ASSERT_CHAR(args, in, '(', "expect " QUOTE_FMT("(") " after " QUOTE_FMT("while") "\n");
				fprintf(out, ".L.start.%lu:\n", id);
				expression(args, in, out, 15);
				/* Zero exits the loop, non-zero continues to the body. */
				fprintf(out, "  cmp $0, %%rax\n" "  je .L.end.%lu\n", id);
				whitespace(args, in);
				ASSERT_CHAR(args, in, ')', "expect " QUOTE_FMT(")") " after condition\n");

				/* Body may be either a block or a single statement. */
				statement(args, in, out, fn_ident, -1, NULL);
				/* Jump back to the loop head after every iteration. */
				fprintf(out, "  jmp .L.start.%lu\n.L.end.%lu:\n", id, id);
				return;
			}
			else if (strcmp(buffer, "switch") == 0) { /* switch statement */ id = stmt_id++;

				/* Evaluate the switch expression once; case comparisons are emitted later. */
				expression(args, in, out, 15);
				fprintf(out, "  jmp .L.cmp.%ld\n.L.stmts.%ld:\n", id, id);

				/* Cases are collected while parsing the body, then emitted as comparisons. */
				memset(&switch_case_list, 0, sizeof(struct list));
				statement(args, in, out, fn_ident, id, &switch_case_list);
				fprintf(out, "  jmp .L.end.%ld\n" ".L.cmp.%ld:\n", id, id);

				/* Emit one comparison per recorded case value. */
				for (i = 0; i < ( intptr_t ) switch_case_list.size; i++)
					fprintf(
					  out, "  cmp $%lu, %%rax\n  je .L.case.%lu.%lu\n", ( uintptr_t ) switch_case_list.data [i], id,
					  ( uintptr_t ) switch_case_list.data [i]
					);

				/* No case matched: fall through to the end label. */
				fprintf(out, ".L.end.%ld:\n", id);

				list_free(&switch_case_list);
				return;
			}
			else if (strcmp(buffer, "case") == 0) { /* case statement */ id = stmt_id++;

				/* A case only makes sense inside a switch. */
				if (switch_id < 0) {
					eprintf_pos(
					  &args->pos, "unexpected " QUOTE_FMT("case") " outside of " QUOTE_FMT("switch") " statements\n"
					);
					exit(1);
				}

				/* Case constants may be character or numeric literals. */
				switch (c = fgetc(in)) {
				case '\'' : value = character(args, in); break;
				default :
					if (isdigit(c)) {
						ungetc(c, in);
						value = number(args, in);
						break;
					}

					eprintf_pos(
					  &args->pos,
					  "unexpected character " QUOTE_FMT("%c") ", expect constant after " QUOTE_FMT("case") "\n", c
					);
					exit(1);
				}

				if (( intptr_t ) value == EOF) {
					eprintf_pos(&args->pos, "unexpected end of file, expect constant after " QUOTE_FMT("case") "\n");
					exit(1);
				}
				whitespace(args, in);
				ASSERT_CHAR(args, in, ':', "expect " QUOTE_FMT(":") " after " QUOTE_FMT("case") "\n");
				/* Record the case value so the parent switch can emit the compare table. */
				list_push(cases, ( void * ) value);

				/* Emit a dedicated label for the case body. */
				fprintf(out, ".L.case.%ld.%lu:\n", switch_id, value);
				statement(args, in, out, fn_ident, switch_id, cases);
				return;
			}
			else if (strcmp(buffer, "extrn") == 0) { /* external declaration */ do {
					/* extrn declares names visible to this function but defined elsewhere. */
					if (!identifier(args, in, buffer)) {
						eprintf_pos(&args->pos, "expect identifier after " QUOTE_FMT("extrn") "\n");
						exit(1);
					}

					/* Reject duplicate names in the same function scope. */
					if (find_identifier(args, buffer, NULL) >= 0) {
						eprintf_pos(
						  &args->pos, "identifier " QUOTE_FMT("%s") " is already defined in this scope\n", buffer
						);
						exit(1);
					}

					/* Store a heap copy so the identifier survives parser buffer reuse. */
					list_push(&args->extrns, strdup(buffer));

					/* extrn lists are comma-separated. */
					whitespace(args, in);
				}
				while ((c = fgetc(in)) == ',');

				/* The declaration must end with a semicolon, not another token. */
				if (c != ';') {
					eprintf_pos(
					  &args->pos,
					  "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT(";") " or " QUOTE_FMT(",") "\n", c
					);
					exit(1);
				}
				return;
			}
			else if (strcmp(buffer, "auto") == 0) {
				/* auto introduces stack storage, optionally with a vector size. */
				do {
					/* Each declared name must be unique in the current function scope. */
					if (!identifier(args, in, buffer)) {
						eprintf_pos(&args->pos, "expect identifier after " QUOTE_FMT("auto") "\n");
						exit(1);
					}
					if (find_identifier(args, buffer, NULL) >= 0) {
						eprintf_pos(
						  &args->pos, "identifier " QUOTE_FMT("%s") " is already defined in this scope\n", buffer
						);
						exit(1);
					}
					whitespace(args, in);

					/* Default to scalar storage unless brackets or an immediate size follow. */
					value = -1;
					if ((c = fgetc(in)) == '\'') {
						value = character(args, in);
						whitespace(args, in);
						c = fgetc(in);
					}
					else if (c == '[') {
						/* Vector declarations use a word count inside brackets. */
						value = number(args, in);
						whitespace(args, in);
						if ((c = fgetc(in)) != ']') {
							eprintf_pos(
							  &args->pos, "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT("]") "\n", c
							);
							exit(1);
						}
						whitespace(args, in);
						c = fgetc(in);
					}
					else if (isdigit(c)) {
						/* Historical sugar: a bare size after the name also means vector storage. */
						ungetc(c, in);
						value = number(args, in);
						whitespace(args, in);
						c = fgetc(in);
					}

					if (value < 0) {
						/* Scalar: allocate one word on the stack. */
						list_push(&args->locals, init_stack_var(buffer, args->stack_offset));
						args->stack_offset += 1;
						fprintf(out, "  sub $%u, %%rsp\n", args->word_size);
					}
					else {
						/* Vector: allocate one extra word to hold the base pointer. */
						list_push(&args->locals, init_stack_var(buffer, args->stack_offset + value));
						args->stack_offset += value + 1;
						fprintf(out, "  sub $%lu, %%rsp\n", args->word_size * (value + 1));

						/* Store the address of the vector payload in the pointer slot. */
						fprintf(out, "  lea -%lu(%%rbp), %%rax\n", args->stack_offset * args->word_size);
						fprintf(out, "  movq %%rax, -%lu(%%rbp)\n", (args->stack_offset + 1) * args->word_size);
					}
				}
				while ((c) == ',');

				/* auto declarations end with a semicolon. */
				if (c != ';') {
					eprintf_pos(
					  &args->pos,
					  "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT(";") " or " QUOTE_FMT(",") "\n", c
					);
					exit(1);
				}

				/* Keep stack slots even, giving 16-byte alignment on x86_64. */
				if (args->stack_offset % 2) {
					fprintf(out, "  sub $%u, %%rsp\n", args->word_size);
					args->stack_offset++;
				}
				return;
			}
			else {
				switch (c = fgetc(in)) {
				case ':' : /* label */
					/* Labels live in the function namespace, not the global one. */
					fprintf(out, ".L.label.%s.%s:\n", buffer, fn_ident);
					statement(args, in, out, fn_ident, switch_id, cases);
					return;
				default :
					/* Not a keyword/label: reparse the identifier as an expression. */
					ungetc(c, in);
					for (i = strlen(buffer) - 1; i >= 0; i--) ungetc(buffer [i], in);

					expression(args, in, out, 15);
					whitespace(args, in);
					/* Expression statements still need a terminating semicolon. */
					if ((c = fgetc(in)) != ';') {
						eprintf_pos(
						  &args->pos,
						  "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT(
							";"
						  ) " after expression statement\n",
						  c
						);
						exit(1);
					}
				}
			}
		}
		else if (c == EOF) {
			/* EOF while expecting a statement is an unrecoverable parse error. */
			eprintf_pos(&args->pos, "unexpected end of file, expect statement\n");
			exit(1);
		}
		else {
			/* Bare expressions are allowed as statements too. */
			ungetc(c, in);
			expression(args, in, out, 15);
			whitespace(args, in);
			/* A statement-level expression still needs a semicolon. */
			if ((c = fgetc(in)) != ';') {
				eprintf_pos(
				  &args->pos,
				  "unexpected character " QUOTE_FMT("%c") " expect " QUOTE_FMT(";") " after expression statement\n", c
				);
				exit(1);
			}
		}
	}
}

//
// Parse a list of function arguments.
//
static void arguments(struct compiler_args *args, FILE *in, FILE *out) {
	int         c;
	int         i = 0;
	static char buffer [BUFSIZ];

	/* Parse a comma-separated parameter list until the closing parenthesis. */
	while (1) {
		whitespace(args, in);
		if (!identifier(args, in, buffer)) {
			eprintf_pos(&args->pos, "expect " QUOTE_FMT(")") " or identifier after function arguments\n");
			exit(1);
		}
		/*
		 * SysV ABI delivers the first six integer/pointer arguments in registers.
		 * BCause immediately spills each one into its stack-frame model, because the
		 * rest of the compiler knows how to address locals but not live parameters.
		 */
		fprintf(
		  out, "  sub $%u, %%rsp\n  mov %s, -%lu(%%rbp)\n", args->word_size, arg_registers [i++],
		  (args->stack_offset + 2) * args->word_size
		);

		/* Parameters become ordinary locals after the initial spill. */
		list_push(&args->locals, init_stack_var(strdup(buffer), args->stack_offset++));

		/* Either finish the parameter list or require a comma. */
		whitespace(args, in);
		switch (c = fgetc(in)) {
		case ')' : return;
		case ',' : continue;
		default :
			eprintf_pos(
			  &args->pos, "unexpected character " QUOTE_FMT("%c") ", expect " QUOTE_FMT(")") " or " QUOTE_FMT(",") "\n",
			  c
			);
			exit(1);
		}
	}
}

//
// Parse a function definition.
//
static void function(struct compiler_args *args, FILE *in, FILE *out, char *fn_id) {
	size_t i;
	int    c;

	/* Each function starts with a fresh local symbol table and empty stack model. */
	for (i = 0; i < args->locals.size; i++) free_stack_var(( struct stack_var * ) args->locals.data [i]);
	list_clear(&args->locals);
	args->stack_offset = 0;

	/* Externals are also function-scoped in this compiler's state object. */
	for (i = 0; i < args->extrns.size; i++)
		if (args->extrns.data [i] != fn_id) free(args->extrns.data [i]);
	list_clear(&args->extrns);

	/* Let the function name resolve as a callable external inside its own body. */
	list_push(&args->extrns, fn_id);

	/* Emit the function label and a simple rbp-based stack frame prologue. */
	fprintf(out,
        ".text\n"
        ".type %s, @function\n"
        "%s:\n"
        "  push %%rbp\n"
        "  mov %%rsp, %%rbp\n"
        "  sub $%d, %%rsp\n",
        fn_id, fn_id, args->word_size
    );

	/* If the next byte is not `)`, parse formal parameters. */
	if ((c = fgetc(in)) != ')') {
		ungetc(c, in);
		arguments(args, in, out);
	}

	/* The whole function body is parsed as one statement, usually a block. */
	statement(args, in, out, fn_id, -1, NULL);

	/* Default fallthrough return is zero; explicit returns jump to this epilogue. */
	fprintf(out, "  xor %%rax, %%rax\n" ".L.return.%s:\n" "  mov %%rbp, %%rsp\n" "  pop %%rbp\n" "  ret\n", fn_id);
}

//
// Create read-only section with strings.
//
static void strings(struct compiler_args *args, FILE *out) {
	char  *string;
	size_t i, j, size;

	/* String literals collected during parsing are emitted together as read-only data. */
	fprintf(out, ".section .rodata\n");

	for (i = 0; i < args->strings.size; i++) {
		/* Labels are numeric and match the indices used when literals were parsed. */
		fprintf(out, ".string.%lu:\n", i);

		string = ( char * ) args->strings.data [i];
		size   = strlen(string);
		/* Emit bytes manually instead of using .string, so B escapes are already resolved. */
		for (j = 0; j < size; j++) fprintf(out, "  .byte %u\n", string [j]);
		/* C-style NUL terminator for runtime string functions. */
		fprintf(out, "  .byte 0\n");

		/* Ownership of parsed string buffers ends after emission. */
		free(string);
	}

	/* Free the backing table after all string entries are flushed. */
	list_free(&args->strings);
}

//
// Parse top level declarations:
//      name(...    -- function definition
//      name[...    -- vector declaration
//      name...     -- scalar declaration
//
static void declarations(struct compiler_args *args, FILE *in, FILE *out) {
	static char buffer [BUFSIZ];
	int         c;
	size_t      i;

	/* Top-level grammar is intentionally tiny: every declaration starts by name. */
	while (identifier(args, in, buffer)) {
		/* Every top-level name is exported. Subtle? No. Very Unix? Yes. */
		fprintf(out, ".globl %s\n", buffer);

		/* One-character lookahead classifies the declaration kind. */
		switch (c = fgetc(in)) {
		/* name(...) is a function definition. */
		case '(' : function(args, in, out, buffer); break;

		/* name[...] is a global vector declaration. */
		case '[' : vector(args, in, out, buffer); break;

		/* A name cannot be the final byte of a valid declaration. */
		case EOF : eprintf_pos(&args->pos, "unexpected end of file after declaration\n"); exit(1);

		/* Anything else belongs to a scalar declaration and must be returned. */
		default  : ungetc(c, in); global(args, in, out, buffer);
		}
	}

	/* If identifier() stopped but file is not exhausted, the top-level token is invalid. */
	if (fgetc(in) != EOF) {
		eprintf_pos(&args->pos, "expect identifier at top level\n");
		exit(1);
	}

	/* Emit all string literals discovered while parsing this input. */
	strings(args, out);

	/* Final cleanup. In a perfect world this ownership story would be less spicy. */
	for (i = 0; i < args->locals.size; i++) free_stack_var(( struct stack_var * ) args->locals.data [i]);
	list_free(&args->locals);
	args->stack_offset = 0;

	/* Externals are mostly heap strings, except current parser buffers may be borrowed. */
	for (i = 0; i < args->extrns.size; i++)
		if (args->extrns.data [i] != buffer) free(args->extrns.data [i]);
	list_free(&args->extrns);
}
