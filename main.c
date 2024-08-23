#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma pack(1)

typedef uint64_t PTR;

struct List
{
	PTR last, first;
	uint8_t node_size;
};

#define NEXT(node) (*(PTR*)(node))
#define NEXT_S(node, list) ((node) ? NEXT(node) : list.first)
#define LLDATA_PTR(node, T) ((struct T*)(node + sizeof(PTR)))
#define AsHEX(x) ((char)((x) + ((x) <= 10 ? '0' : 55)))
#define ifErr(case, no, ret) if (case) { errorno = no; return ret; }

uint16_t errorno = 0;

uint8_t* output;
uint32_t origin_addr = 0, addr = 0;
uint16_t addr_index = 0, line_index = 0;

char* global_crnt = 0;

const char* error_names[] = {
	"No error.",
	"malloc() return 0.",
	"Unknown register.",
	"[Removed error] (Register capital characters mismatch)", // "register capital characters mismatch"
	"Label is already set.",
	"Input file name didn't given.",
	"Failed to open the file.",
	"When using adressing used same type twice [%EAX, 4%EBX, 0x7F, %EAX <- Error].",
	"When inputing the IMM in adressing you used non IMM.",
	"No alpha at start of case.",
	"Unknown instructon.",
	"Instructons' all characters must be capital.",
	"First type instruction called after Non type instruction. (ex: CMC \\n ORG) {addr_index == 0}",
	"Extra is given twice.",
	"Too many arguments.",
	"Arguments must split with a comma. (',')",
	"Unknown argument.", // 16
	"Wrong amount of arguments.",
	"Useless Extra input.",
	"Wrong type of arguments.",
	"Comma before first argument.",
	"No space before first argument.", // 21
	"Extra's name length equals to zero. (Use uppercase letters)",
	"Unknown extra.", // 23
	"Extra can't be 0.",
	"Unknown extra. (while processing)",
	"Not defined size (extra) in 'DEF' instruction. (Use 'DEF.BYTE')" // 26
};

void List_push(struct List* list)
{
	PTR node = malloc(list->node_size);
	ifErr(node == 0, 1, )
		NEXT(node) = 0;
	if (list->last) NEXT(list->last) = node;
	else list->first = node;
	list->last = node;
}

void List_pop(struct List* list, PTR prev_node)
{
	PTR node, next;
	if (prev_node == 0)
	{
		node = list->first;
		next = NEXT(node);
		list->first = next;
	}
	else
	{
		node = NEXT(prev_node);
		next = NEXT(node);
		NEXT(prev_node) = next;
	}

	if (next == 0)
		list->last = prev_node;

	free(node);
}

void List_destroy(struct List* list)
{
	for (PTR node = list->first, next; node; node = next)
	{
		next = NEXT(node);
		free(node);
	}

	list->last = 0;
	list->first = 0;
}

struct Chars
{
	uint8_t length;
	char* start;
};

inline uint8_t Chars_compare(struct Chars cs0, struct Chars cs1)
{
	if (cs0.length != cs1.length)
		return 0xFF;
	return strncmp(cs0.start, cs1.start, cs0.length);
}

struct Label
{
	struct Chars name;
	uint32_t value;
};

struct Waiter
{
	struct Chars label;
	uint16_t index;
	uint8_t size;
};

struct List labels = { 0, 0, sizeof(struct Label) + 8 };
struct List waiters = { 0, 0, sizeof(struct Waiter) + 8 };

static void print_labels()
{
	if (labels.last == 0)
	{
		puts("empty list (labels)");
		return;
	}

	for (PTR node = labels.first; node; node = NEXT(node))
	{
		struct Label* label = LLDATA_PTR(node, Label);
		char* tr = label->name.start + label->name.length, c = *tr;
		*tr = '\0';
		printf("'%s': %x\n", label->name.start, label->value);
		*tr = c;
	}
}

static void print_waiters()
{
	if (waiters.last == 0)
	{
		puts("empty list (waiters)");
		return;
	}

	for (PTR node = waiters.first; node; node = NEXT(node))
	{
		struct Waiter* waiter = LLDATA_PTR(node, Waiter);
		char* tr = waiter->label.start + waiter->label.length, c = *tr;
		*tr = '\0';
		printf("%x(%d): '%s'\n", waiter->index, waiter->size, waiter->label.start);
		*tr = c;
	}
}

uint32_t findNget_label_value(struct Chars cs)
{
	for (PTR node = labels.first; node; node = NEXT(node))
		if (Chars_compare(cs, LLDATA_PTR(node, Label)->name) == 0)
			return LLDATA_PTR(node, Label)->value;
	return 0;
}

void add_label(struct Label label)
{
	ifErr(findNget_label_value(label.name), 4, )
		List_push(&labels);
	*LLDATA_PTR(labels.last, Label) = label;
	for (PTR node = waiters.first, prev = 0; node;)
	{
		if (Chars_compare(label.name, LLDATA_PTR(node, Waiter)->label) == 0)
		{
			uint8_t* bytes = output + LLDATA_PTR(node, Waiter)->index;

			switch (LLDATA_PTR(node, Waiter)->size)
			{
			case 0: *(int8_t*)bytes += label.value; break;
			case 1: *(int16_t*)bytes += label.value; break;
			case 2: *(int32_t*)bytes += label.value; break;
			}

			List_pop(&waiters, prev);

			node = NEXT_S(prev, waiters);
			continue;
		}

		prev = node;
		node = NEXT(node);
	}
}

inline void add_byte(uint8_t x) { output[addr_index] = x; ++addr_index; }
inline void add_word(uint16_t x) { *(uint16_t*)(output + addr_index) = x; addr_index += 2; }
inline void add_dword(uint32_t x) { *(uint32_t*)(output + addr_index) = x; addr_index += 4; }

static void print_output()
{
	static char buf[] = "XX ";
	printf("\t-%x-\n0:\t", origin_addr);
	for (uint16_t i = 0; i < addr_index;)
	{
		uint8_t x = output[i];
		buf[0] = AsHEX(x >> 4);
		buf[1] = AsHEX(x & 0b1111);
		if ((++i & 0b111) || i == addr_index)
			fputs(buf, stdout);
		else
			printf("%s\n%x:\t", buf, i);
	}
	putchar('\n');
}

/*
EAX AX AL
ECX CX CL
EDX DX DL
EBX BX BL
ESP SP AH
EBP BP CH
ESI SI DH
EDI DI BH
*/

#define REGISTER_BITS 6
// 0b00SSRXXX (R = reserved for future)
uint8_t GP_register_value(char* name)
{
	uint8_t data = 0b10000;
	char c0 = *name, c1;

	if (c0 == 'E' || c0 == 'e')
	{
		c1 = name[2];
		// ifErr((c1 <= 'Z') != (c0 == 'E'), 3, 0xFF)
		c0 = name[1];
		data = 0b100000;
	}
	else
		c1 = name[1];

	/*
	ifErr((c0 >= 'a') != (c1 > 'Z'), 3, 0xFF)

	if (c0 >= 'a')
	{
		c0 -= 32;
		c1 -= 32;
	}
	*/

	switch (c1)
	{
	case 'H':
		data |= 0b100;
	case 'L':
		ifErr(data & 0b100000, 2, 0xFF)
			data &= 0b1111;
	case 'X':
		switch (c0)
		{
		case 'A': return data;
		case 'C': return data | 1;
		case 'D': return data | 2;
		case 'B': return data | 3;
		}
		break;
	case 'P':
		switch (c0)
		{
		case 'S': return data | 4;
		case 'B': return data | 5;
		}
		break;
	case 'I':
		switch (c0)
		{
		case 'S': return data | 6;
		case 'D': return data | 7;
		}
		break;
	}

	errorno = 2;
	return 0xFF;
}

char _VTR_buffer[] = "EXX"; // value_to_register
const char* value_to_register(uint8_t value)
{
	static const char ACDB_SBSD[] =
	{ 'A', 'C', 'D', 'B', 'S', 'B', 'S', 'D' };

	if ((value & 0b100) == 0 || (value & 0b110000) == 0)
	{
		_VTR_buffer[1] = ACDB_SBSD[value & 0b11];
		_VTR_buffer[2] = (value & 0b110000) ? 'X' : ((value & 0b100) ? 'H' : 'L');
	}
	else
	{
		_VTR_buffer[1] = (ACDB_SBSD + 4)[value & 0b11];
		_VTR_buffer[2] = (value & 0b10) ? 'I' : 'P';
	}

	return (value & 0b100000) ? _VTR_buffer : (_VTR_buffer + 1);
}

inline uint8_t is_lower(uint8_t c) { return (uint8_t)(c - 'a') < 26; }
inline uint8_t is_capital(uint8_t c) { return (uint8_t)(c - 'A') < 26; }
inline uint8_t is_digit(uint8_t c) { return (uint8_t)(c - '0') < 10; }

inline uint8_t is_alphau(char c) { return c == '_' || is_capital(c) || is_lower(c); }
inline uint8_t is_alnumu(char c) { return is_alphau(c) || is_digit(c); }
inline uint8_t is_space(uint8_t c) { return (uint8_t)(c - 1) < ' '; }

inline char to_upper(char c) { return (c >= 'a') ? (c - 32) : c; }

inline uint16_t to3capitalvalue(uint8_t c0, uint8_t c1, uint8_t c2)
{
	c0 -= 'A'; c1 -= 'A'; c2 -= 'A';
	if (c0 > 25 || c1 > 25 || c2 > 26) // c2 can be 'Z'+1
		return -1;
	return c0 | (c1 << 5) | (c2 << 10);
}

inline uint32_t to6capitalvalue(char* str)
{
	uint32_t v = 0;
	while (is_capital(*str))
	{
		v = (v << 5) + *str - 64; ++str;
	}
	global_crnt = str;
	return v;
}

uint32_t to_int(char* name)
{
	uint8_t base = 10;
	char c = name[0];
	if (c == '0')
	{
		base = 16;
		switch (name[1])
		{
		case 'B':
		case 'b':
			base = 2;
		case 'X':
		case 'x':
			name += 2;
			c = name[0];
			break;
		default:
			base = 10;
			break;
		}
	}

	uint32_t v = 0;
	while (1)
	{
		if (is_digit(c))
			c -= '0';
		else if (is_capital(c))
			c -= ('A' - 10);
		else if (is_lower(c))
			c -= ('a' - 10);
		else
		{
			global_crnt = name;
			return v;
		}

		v = v * base + c;
		c = *++name;
	}
}

#define AVT_REG 0
#define AVT_WAITER 0x2000000000000000U
#define AVT_IMM 0x4000000000000000U
#define AVT_PTR 0x8000000000000000U
#define AVT_TYPE 0xC000000000000000U

// 3 bit - type | rest - value
// mov arg1, arg0

struct Chars AV_waiter_label = { 0, 0 };

uint64_t arg_value(char* arg)
{
	char c = *arg;
	if (c == '%')
	{
		global_crnt = arg + 2;
		return GP_register_value(arg + 1);
	}

	uint8_t waiter = 0;
	uint32_t value = 0;
	if (c == '[')
	{
		uint8_t reg = 0xFE, sreg = 0xFE, scale = 0;
		goto skip_spaces1;

		while (1)
		{
			if (c == '%')
			{
				++arg;
				if (reg == 0xFE)
					reg = GP_register_value(arg);
				else if (sreg == 0xFE)
					sreg = GP_register_value(arg);
				else
				{
					errorno = 7;
					return -1;
				}
				arg += 2;
			}
			else if (is_digit(c) && arg[1] == '%')
			{
				ifErr(sreg != 0xFE, 7, -1)
					arg += 2;
				sreg = GP_register_value(arg);
				arg += 2;
				scale = c - '0';
			}
			else
			{
				ifErr(value, 7, -1)
					uint64_t t = arg_value(arg);
				if (t == (uint64_t)-1) return -1;
				waiter = ((t & AVT_WAITER) != 0);
				value = t;
				arg = global_crnt;
			}

			while (*arg != ',' && *arg != ']' && *arg != '\0') ++arg;
			c = *arg;
			if (c == '\0') return -1;
			if (c == ']')
			{
				global_crnt = arg + 1;
				return (waiter ? (AVT_WAITER | AVT_PTR) : AVT_PTR) | value | ((uint64_t)reg << 32) |
					((uint64_t)sreg << (32 + REGISTER_BITS)) |
					((uint64_t)scale << (32 + REGISTER_BITS * 2));
			}

		skip_spaces1:
			++arg;
			while (*arg == ' ' || *arg == '\t') ++arg;
			c = *arg;
		}
	}

	uint32_t temp;
	uint8_t sign = 0;
	if (c == '-') { sign = 1; c = '\0'; ++arg; goto skip_spaces0; }
	while (1)
	{
		if (c == '$')
		{
			++arg;
			if (*arg == '$')
			{
				temp = origin_addr;
				++arg;
			}
			else if (is_alphau(*arg))
			{
				uint8_t len = 1;
				while (is_alnumu(arg[len])) ++len;
				temp = findNget_label_value((struct Chars) { len, arg });
				if (temp == 0)
				{
					waiter = 1;
					AV_waiter_label.length = len;
					AV_waiter_label.start = arg;
				}
				arg += len;
			}
			else temp = addr;
		}
		else if (is_digit(c))
		{
			temp = to_int(arg);
			arg = global_crnt;
		}
		else return -1;

		if (sign) value -= temp;
		else value += temp;

	skip_spaces0:
		while (*arg == ' ' || *arg == '\t') ++arg;

		if (c == '\0')
		{
			c = *arg;
			continue;
		}

		if ((sign = (*arg != '+')) && *arg != '-')
		{
			global_crnt = arg;
			if (waiter)
				return (AVT_WAITER | AVT_IMM) | value;
			return AVT_IMM | value;
		}

		++arg;
		c = '\0';
		goto skip_spaces0;
	}
}

uint8_t verbose = 1;

char* output_filename = 0;
uint8_t output_file_auto = 0;

char* debug_filename = 0;

char* input_filename = 0;
uint32_t input_filesize = 0;
char* input_buf = 0;

#define OUTPUT_MAX_SIZE 0x1000

int main(int argc, char** argv)
{
	// Get command arguments "-i <input_path> -v 2 (verbose level 2)"
	uint8_t state = 0;
	for (int i = 1; i < argc; i++)
	{
		switch (state)
		{
		case 0:
			if (argv[i][0] == '-')
			{
				state = argv[i][1];
				continue;
			}
		case 'i':
			input_filename = argv[i];
			break;
		case 'o':
			output_filename = argv[i];
			break;
		case 'd':
			debug_filename = argv[i];
			break;
		case 'v':
			verbose = argv[i][0] - '0';
			break;
		default:
			printf("Unknown state (-%c(%d)): %s\n", state, state, argv[i]);
			return 0xFF;
			break;
		}

		state = 0;
	}

	if (state)
	{
		// errorno = NULL (0xFF);
		printf("Data didn't given for state (-%c(%d))\n", state, state);
		return 0xFF;
	}

	if (input_filename == 0)
	{
		// errorno = 5;
		fputs(error_names[5], stdout);
		return 5;
	}

	if (output_filename == 0) // "f(test.asm) = f(test) = test.bin"
	{
		output_file_auto = 1;
		uint16_t len = strlen(input_filename), dot = len - 2;
		while (input_filename[dot] != '.' && dot != 0) --dot;
		if (dot) len = dot;
		output_filename = malloc(len + 5);
		if (output_filename == 0)
		{
			// errorno = 1;
			fputs(error_names[1], stdout);
			return 1;
		}
		*(uint32_t*)(output_filename + len) = 0x6E69622E; // = ".bin" = ('.' | ('b' >> 8) ...)
		output_filename[len + 4] = '\0';
		strncpy(output_filename, input_filename, len);
		if (verbose)
			printf("Output file (%s) created automatically from input file.\n", output_filename);
	}

	if (verbose)
		printf("Opening file %s . . .\n", input_filename);

	// Read from input file to a buffer
	{
		FILE* file = fopen(input_filename, "rb");

		if (file == 0)
		{
			// errorno = 6;
			fputs(error_names[6], stdout);
			if (output_file_auto)
				free(output_filename);
			return 6;
		}

		fseek(file, 0, SEEK_END);
		input_filesize = ftell(file);
		fseek(file, 0, SEEK_SET);
		input_buf = malloc(input_filesize + 1);
		if (input_buf == 0)
		{
			// errorno = 1;
			fputs(error_names[1], stdout);
			fclose(file);
			if (output_file_auto)
				free(output_filename);
			return 1;
		}
		fread(input_buf, 1, input_filesize, file);
		fclose(file);
		input_buf[input_filesize] = '\0';
	}

	output = malloc(OUTPUT_MAX_SIZE);
	if (output == 0)
	{
		// errorno = 1;
		fputs(error_names[1], stdout);
		free(input_buf);
		if (output_file_auto)
			free(output_filename);
		return 1;
	}

	char* crnt = input_buf;

process_case:

	while (is_space(*crnt)) ++crnt;
	if (*crnt == '\0') goto main_end;

	if (is_alphau(*crnt) == 0)
	{
		errorno = 9;
		goto main_end;
	}
	uint8_t len = 1;
	while (is_alnumu(crnt[len])) ++len;
	if (crnt[len] == ':')
	{
		add_label((struct Label) { { len, crnt }, addr });
		crnt += len + 1;
		goto process_case;
	}

	char* alpha = crnt;
	crnt += len;

	uint8_t args_index = 0;
	uint8_t extra = 0;

	uint64_t args[3] = { 0, 0, 0 };

	uint8_t comma = 0;

case_args:

	while (*crnt == ' ' || *crnt == '\t') ++crnt;
	if (comma) goto process_arg;
	char c = crnt[0];
	if (c == '\n') { ++crnt; goto final_case; }
	if (c == '\r' && crnt[1] == '\n') { crnt += 2; goto final_case; }
	if (c == ',')
	{
		if (args_index == 0) { errorno = 20; goto main_end; }
		comma = 1; ++crnt; goto case_args;
	}
	if (c == '\0') goto final_case;

	if (args_index != 0) { errorno = 15; goto main_end; }

	if (c == '.')
	{
		if (extra != 0) { errorno = 13; goto main_end; }
		++crnt;
		if (is_digit(*crnt))
		{
			if (*crnt == '0') { errorno = 24; goto main_end; }
			extra = *crnt - '0';
			++crnt;
			goto case_args;
		}
		uint32_t value = to6capitalvalue(crnt);
		if (value == 0) { errorno = 22; goto main_end; }
		switch (value)
		{
		case 91781U: extra = 1; break; // "BYTE"
		case 769604U: extra = 2; break; // "WORD"
		case 4963908U: extra = 3; break; // "DWORD"
		default: errorno = 23; goto main_end;
		}
		crnt = global_crnt;
		goto case_args;
	}

	if (crnt[-1] != ' ' && crnt[-1] != '\t') { errorno = 21; goto main_end; }

process_arg:

	if (args_index == 3) { errorno = 14; goto main_end; }
	uint64_t arg = arg_value(crnt);
	if (arg == (uint64_t)-1) { errorno = 16; goto main_end; }
	args[args_index] = arg;
	++args_index;
	crnt = global_crnt;
	comma = 0;

	goto case_args;

final_case:

	if (verbose > 1)
		printf("Case: {%x, %x, %x}[%u] .%u\n", args[0], args[1], args[2], args_index, extra);

	if (len <= 3)
	{
		uint16_t opc = to3capitalvalue(alpha[0], alpha[1], (len == 3) ? alpha[2] : 91);

		switch (opc)
		{
		case 2434: // CMC
			add_byte(0xF5);
			break;
		case 5251: // DEF
			if (args_index != 1) { errorno = 17; goto main_end; }
			if ((uint8_t)(args[0] >> 62) != 1) { errorno = 19; goto main_end; }
			if (args[0] & AVT_WAITER)
			{
				List_push(&waiters);
				LLDATA_PTR(waiters.last, Waiter)->index = addr_index;
				LLDATA_PTR(waiters.last, Waiter)->size = extra - 1;
				LLDATA_PTR(waiters.last, Waiter)->label = AV_waiter_label;
				AV_waiter_label.length = 0;
			}
			switch (extra)
			{
			case 1: add_byte(args[0]); break;
			case 2: add_word(args[0]); break;
			case 3: add_dword(args[0]); break;
			case 0: errorno = 26; goto main_end;
			default: errorno = 25; goto main_end;
			}
			break;
		case 6702: // ORG
			if (addr_index) { errorno = 12; goto main_end; }
			if (args_index != 1) { errorno = 17; goto main_end; }
			if (extra) { errorno = 18; goto main_end; }
			if ((uint8_t)(args[0] >> 62) != 1) { errorno = 19; goto main_end; }
			origin_addr = (uint32_t)args[0];
			break;
		case (uint16_t)-1:
			errorno = 11;
			goto main_end;
		default:
			errorno = 10;
			goto main_end;
		}

		addr = origin_addr + addr_index;
	}
	else
	{
		errorno = 10;
		goto main_end;
	}

	if (c != '\0')
		goto process_case;

main_end:

	print_output();
	print_labels();
	print_waiters();

	// Clean up
	if (output_file_auto)
		free(output_filename);

	free(output);
	List_destroy(&labels);
	List_destroy(&waiters);
	fputs(error_names[errorno], stdout);
	return errorno;
}
