/*
*   Copyright (c) 2003, Darren Hiebert
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*
*   This module contains functions for generating tags for the Verilog HDL
*   (Hardware Description Language).
*
*   Language definition documents:
*       http://www.eg.bucknell.edu/~cs320/verilog/verilog-manual.html
*       http://www.sutherland-hdl.com/on-line_ref_guide/vlog_ref_top.html
*       http://www.verilog.com/VerilogBNF.html
*       http://eesun.free.fr/DOC/VERILOG/verilog_manual1.html
*/

/*
 *   INCLUDE FILES
 */
#include "general.h"  /* must always come first */

#include <string.h>

#include "debug.h"
#include "entry.h"
#include "keyword.h"
#include "options.h"
#include "parse.h"
#include "read.h"
#include "routines.h"
#include "xtag.h"

/*
*   MACROS
*/
#define NUMBER_LANGUAGES    2   /* Indicates number of defined indexes */
#define IDX_SYSTEMVERILOG   0
#define IDX_VERILOG         1

/*
 *   DATA DECLARATIONS
 */

/* A callback function searching a symbol from the cork symbol table assumes
 * this kind definitions are shared in Verilog and SystemVerilog parsers.
 * If you will separate the definitions for the parsers, you must revise the
 * code related to the symbol table. */
typedef enum {
	/* parser private items */
	K_IGNORE = -16,	/* Verilog/SystemVerilog keywords to be ignored */
	K_DEFINE,
	K_DIRECTIVE,
	K_BEGIN,
	K_END,
	K_END_DE,	/* End of Design Elements */
	K_IDENTIFIER,
	K_LOCALPARAM,
	K_PARAMETER,

	K_UNDEFINED = KEYWORD_NONE,
	/* the followings items are also used as indices for VerilogKinds[] and SystemVerilogKinds[] */
	K_CONSTANT= 0,
	K_EVENT,
	K_FUNCTION,
	K_MODULE,
	K_NET,
	K_PORT,
	K_REGISTER,
	K_TASK,
	K_BLOCK,
	K_ASSERTION,
	K_CLASS,
	K_COVERGROUP,
	K_ENUM,
	K_INTERFACE,
	K_MODPORT,
	K_PACKAGE,
	K_PROGRAM,
	K_PROTOTYPE,
	K_PROPERTY,
	K_STRUCT,
	K_TYPEDEF
} verilogKind;

typedef struct {
	const char *keyword;
	verilogKind kind;
	short isValid [NUMBER_LANGUAGES];
} keywordAssoc;

typedef struct sTokenInfo {
	verilogKind         kind;
	vString*            name;          /* the name of the token */
	unsigned long       lineNumber;    /* line number where token was found */
	MIOPos              filePosition;  /* file position where token was found */
	struct sTokenInfo*  scope;         /* context of keyword */
	int                 nestLevel;     /* Current nest level */
	verilogKind         lastKind;      /* Kind of last found tag */
	vString*            blockName;     /* Current block name */
	vString*            inheritance;   /* Class inheritance */
	bool                prototype;     /* Is only a prototype */
	bool                classScope;    /* Context is local to the current sub-context */
	bool				parameter;	   /* parameter which can be overridden */
	bool				hasParamList;  /* module definition has a parameter port list */
} tokenInfo;

typedef enum {
	F_PARAMETER,
} verilogField;

/*
 *   DATA DEFINITIONS
 */
static int Ungetc;
static int Lang_verilog;
static int Lang_systemverilog;

static kindDefinition VerilogKinds [] = {
 { true, 'c', "constant",  "constants (define, parameter, specparam)" },
 { true, 'e', "event",     "events" },
 { true, 'f', "function",  "functions" },
 { true, 'm', "module",    "modules" },
 { true, 'n', "net",       "net data types" },
 { true, 'p', "port",      "ports" },
 { true, 'r', "register",  "register data types" },
 { true, 't', "task",      "tasks" },
 { true, 'b', "block",     "blocks" }
};

static kindDefinition SystemVerilogKinds [] = {
 { true, 'c', "constant",  "constants (define, parameter, specparam, enum values)" },
 { true, 'e', "event",     "events" },
 { true, 'f', "function",  "functions" },
 { true, 'm', "module",    "modules" },
 { true, 'n', "net",       "net data types" },
 { true, 'p', "port",      "ports" },
 { true, 'r', "register",  "register data types" },
 { true, 't', "task",      "tasks" },
 { true, 'b', "block",     "blocks" },
 { true, 'A', "assert",    "assertions" },
 { true, 'C', "class",     "classes" },
 { true, 'V', "covergroup","covergroups" },
 { true, 'E', "enum",      "enumerators" },
 { true, 'I', "interface", "interfaces" },
 { true, 'M', "modport",   "modports" },
 { true, 'K', "package",   "packages" },
 { true, 'P', "program",   "programs" },
 { false,'Q', "prototype", "prototypes" },
 { true, 'R', "property",  "properties" },
 { true, 'S', "struct",    "structs and unions" },
 { true, 'T', "typedef",   "type declarations" }
};

static const keywordAssoc KeywordTable [] = {
	/*                 	             	  SystemVerilog */
	/*                 	             	  |  Verilog    */
	/* keyword         	keyword ID   	  |  |          */
	{ "`define",       	K_DEFINE,   	{ 1, 1 } },
	{ "begin",         	K_BEGIN,     	{ 1, 1 } },
	{ "end",           	K_END,       	{ 1, 1 } },
	{ "endfunction",   	K_END_DE,    	{ 1, 1 } },
	{ "endmodule",     	K_END_DE,    	{ 1, 1 } },
	{ "endtask",       	K_END_DE,    	{ 1, 1 } },
	{ "event",         	K_EVENT,     	{ 1, 1 } },
	{ "fork",          	K_BEGIN,     	{ 1, 1 } },
	{ "function",      	K_FUNCTION,  	{ 1, 1 } },
	{ "genvar",        	K_REGISTER,  	{ 1, 1 } },
	{ "inout",         	K_PORT,      	{ 1, 1 } },
	{ "input",         	K_PORT,      	{ 1, 1 } },
	{ "integer",       	K_REGISTER,  	{ 1, 1 } },
	{ "join",          	K_END,       	{ 1, 1 } },
	{ "localparam",    	K_LOCALPARAM,  	{ 1, 1 } },
	{ "module",        	K_MODULE,    	{ 1, 1 } },
	{ "output",        	K_PORT,      	{ 1, 1 } },
	{ "parameter",     	K_PARAMETER,  	{ 1, 1 } },
	{ "real",          	K_REGISTER,  	{ 1, 1 } },
	{ "realtime",      	K_REGISTER,  	{ 1, 1 } },
	{ "reg",           	K_REGISTER,  	{ 1, 1 } },
	{ "signed",        	K_IGNORE,    	{ 1, 1 } },
	{ "specparam",     	K_CONSTANT,  	{ 1, 1 } },
	{ "supply0",       	K_NET,       	{ 1, 1 } },
	{ "supply1",       	K_NET,       	{ 1, 1 } },
	{ "task",          	K_TASK,      	{ 1, 1 } },
	{ "time",          	K_REGISTER,  	{ 1, 1 } },
	{ "tri",           	K_NET,       	{ 1, 1 } },
	{ "triand",        	K_NET,       	{ 1, 1 } },
	{ "trior",         	K_NET,       	{ 1, 1 } },
	{ "trireg",        	K_NET,       	{ 1, 1 } },
	{ "tri0",          	K_NET,       	{ 1, 1 } },
	{ "tri1",          	K_NET,       	{ 1, 1 } },
	{ "uwire",         	K_NET,       	{ 1, 1 } },
	{ "wand",          	K_NET,       	{ 1, 1 } },
	{ "wire",          	K_NET,       	{ 1, 1 } },
	{ "wor",           	K_NET,       	{ 1, 1 } },
	{ "assert",        	K_ASSERTION, 	{ 1, 0 } },
	{ "assume",        	K_ASSERTION, 	{ 1, 0 } },
	{ "bit",           	K_REGISTER,  	{ 1, 0 } },
	{ "byte",          	K_REGISTER,  	{ 1, 0 } },
	{ "chandle",       	K_REGISTER,  	{ 1, 0 } },
	{ "class",         	K_CLASS,     	{ 1, 0 } },
	{ "cover",         	K_ASSERTION, 	{ 1, 0 } },
	{ "covergroup",    	K_COVERGROUP,	{ 1, 0 } },
	{ "endclass",      	K_END_DE,    	{ 1, 0 } },
	{ "endgroup",      	K_END_DE,    	{ 1, 0 } },
	{ "endinterface",  	K_END_DE,    	{ 1, 0 } },
	{ "endpackage",    	K_END_DE,    	{ 1, 0 } },
	{ "endprogram",    	K_END_DE,    	{ 1, 0 } },
	{ "endproperty",   	K_END_DE,    	{ 1, 0 } },
	{ "enum",          	K_ENUM,      	{ 1, 0 } },
	{ "extern",        	K_PROTOTYPE, 	{ 1, 0 } },
	{ "int",           	K_REGISTER,  	{ 1, 0 } },
	{ "interconnect",  	K_NET,       	{ 1, 0 } },
	{ "interface",     	K_INTERFACE, 	{ 1, 0 } },
	{ "join_any",      	K_END,       	{ 1, 0 } },
	{ "join_none",     	K_END,       	{ 1, 0 } },
	{ "logic",         	K_REGISTER,  	{ 1, 0 } },
	{ "longint",       	K_REGISTER,  	{ 1, 0 } },
	{ "modport",       	K_MODPORT,   	{ 1, 0 } },
	{ "package",       	K_PACKAGE,   	{ 1, 0 } },
	{ "program",       	K_PROGRAM,   	{ 1, 0 } },
	{ "property",      	K_PROPERTY,  	{ 1, 0 } },
	{ "pure",          	K_PROTOTYPE, 	{ 1, 0 } },
	{ "ref",           	K_PORT,      	{ 1, 0 } },
	{ "sequence",      	K_PROPERTY,  	{ 1, 0 } },
	{ "shortint",      	K_REGISTER,  	{ 1, 0 } },
	{ "shortreal",     	K_REGISTER,  	{ 1, 0 } },
	{ "string",        	K_REGISTER,  	{ 1, 0 } },
	{ "struct",        	K_STRUCT,    	{ 1, 0 } },
	{ "type",          	K_REGISTER,  	{ 1, 0 } },
	{ "typedef",       	K_TYPEDEF,   	{ 1, 0 } },
	{ "union",         	K_STRUCT,    	{ 1, 0 } },
	{ "var",           	K_REGISTER,  	{ 1, 0 } },
	{ "void",          	K_REGISTER,  	{ 1, 0 } }
};

static tokenInfo *currentContext = NULL;
static tokenInfo *tagContents = NULL;
static fieldDefinition *fieldTable = NULL;

// IEEE Std 1364-2005 LRM, Appendix B "List of Keywords"
const static struct keywordGroup verilogKeywords = {
	.value = K_IGNORE,
	.addingUnlessExisting = true,
	.keywords = {
		"always", "and", "assign", "automatic", "begin", "buf", "bufif0",
		"bufif1", "case", "casex", "casez", "cell", "cmos", "config",
		"deassign", "default", "defparam", "design", "disable", "edge",
		"else", "end", "endcase", "endconfig", "endfunction", "endgenerate",
		"endmodule", "endprimitive", "endspecify", "endtable", "endtask",
		"event", "for", "force", "forever", "fork", "function", "generate",
		"genvar", "highz0", "highz1", "if", "ifnone", "incdir", "include",
		"initial", "inout", "input", "instance", "integer", "join", "large",
		"liblist", "library", "localparam", "macromodule", "medium", "module",
		"nand", "negedge", "nmos", "nor", "noshowcancelled", "not", "notif0",
		"notif1", "or", "output", "parameter", "pmos", "posedge", "primitive",
		"pull0", "pull1", "pulldown", "pullup", "pulsestyle_onevent",
		"pulsestyle_ondetect", "rcmos", "real", "realtime", "reg", "release",
		"repeat", "rnmos", "rpmos", "rtran", "rtranif0", "rtranif1",
		"scalared", "showcancelled", "signed", "small", "specify",
		"specparam", "strong0", "strong1", "supply0", "supply1", "table",
		"task", "time", "tran", "tranif0", "tranif1", "tri", "tri0", "tri1",
		"triand", "trior", "trireg", "unsigned1", "use", "uwire", "vectored",
		"wait", "wand", "weak0", "weak1", "while", "wire", "wor", "xnor", "xor",
		NULL
	},
};
// IEEE Std 1800-2017 LRM, Annex B "Keywords"
const static struct keywordGroup systemVerilogKeywords = {
	.value = K_IGNORE,
	.addingUnlessExisting = true,
	.keywords = {
		"accept_on", "alias", "always", "always_comb", "always_ff",
		"always_latch", "and", "assert", "assign", "assume", "automatic",
		"before", "begin", "bind", "bins", "binsof", "bit", "break", "buf",
		"bufif0", "bufif1", "byte", "case", "casex", "casez", "cell",
		"chandle", "checker", "class", "clocking", "cmos", "config", "const",
		"constraint", "context", "continue", "cover", "covergroup",
		"coverpoint", "cross", "deassign", "default", "defparam", "design",
		"disable", "dist", "do", "edge", "else", "end", "endcase",
		"endchecker", "endclass", "endclocking", "endconfig", "endfunction",
		"endgenerate", "endgroup", "endinterface", "endmodule", "endpackage",
		"endprimitive", "endprogram", "endproperty", "endspecify",
		"endsequence", "endtable", "endtask", "enum", "event", "eventually",
		"expect", "export", "extends", "extern", "final", "first_match",
		"for", "force", "foreach", "forever", "fork", "forkjoin", "function",
		"generate", "genvar", "global", "highz0", "highz1", "if", "iff",
		"ifnone", "ignore_bins", "illegal_bins", "implements", "implies",
		"import", "incdir", "include", "initial", "inout", "input", "inside",
		"instance", "int", "integer", "interconnect", "interface",
		"intersect", "join", "join_any", "join_none", "large", "let",
		"liblist", "library", "local", "localparam", "logic", "longint",
		"macromodule", "matches", "medium", "modport", "module", "nand",
		"negedge", "nettype", "new", "nexttime", "nmos", "nor",
		"noshowcancelled", "not", "notif0", "notif1", "null", "or", "output",
		"package", "packed", "parameter", "pmos", "posedge", "primitive",
		"priority", "program", "property", "protected", "pull0", "pull1",
		"pulldown", "pullup", "pulsestyle_ondetect", "pulsestyle_onevent",
		"pure", "rand", "randc", "randcase", "randsequence", "rcmos", "real",
		"realtime", "ref", "reg", "reject_on", "release", "repeat",
		"restrict", "return", "rnmos", "rpmos", "rtran", "rtranif0",
		"rtranif1", "s_always", "s_eventually", "s_nexttime", "s_until",
		"s_until_with", "scalared", "sequence", "shortint", "shortreal",
		"showcancelled", "signed", "small", "soft", "solve", "specify",
		"specparam", "static", "string", "strong", "strong0", "strong1",
		"struct", "super", "supply0", "supply1", "sync_accept_on",
		"sync_reject_on", "table", "tagged", "task", "this", "throughout",
		"time", "timeprecision", "timeunit", "tran", "tranif0", "tranif1",
		"tri", "tri0", "tri1", "triand", "trior", "trireg", "type", "typedef",
		"union", "unique", "unique0", "unsigned", "until", "until_with",
		"untyped", "use", "uwire", "var", "vectored", "virtual", "void",
		"wait", "wait_order", "wand", "weak", "weak0", "weak1", "while",
		"wildcard", "wire", "with", "within", "wor", "xnor", "xor",
		NULL
	},
};

// IEEE Std 1364-2005 LRM, "19. Compiler directives"
const static struct keywordGroup verilogDirectives = {
	.value = K_DIRECTIVE,
	.addingUnlessExisting = true,
	.keywords = {
		"`begin_keywords", "`celldefine", "`default_nettype", "`define",
		"`else", "`elsif", "`end_keywords", "`endcelldefine", "`endif",
		"`ifdef", "`ifndef", "`include", "`line", "`nounconnected_drive",
		"`pragma", "`resetall", "`timescale", "`unconnected_drive", "`undef",
		NULL
	},
};

// IEEE Std 1800-2017 LRM, "22. Compiler directives"
const static struct keywordGroup systemVerilogDirectives = {
	.value = K_DIRECTIVE,
	.addingUnlessExisting = true,
	.keywords = {
		"`__LINE__", "`begin_keywords", "`celldefine", "`default_nettype",
		"`define", "`else", "`elsif", "`end_keywords", "`endcelldefine",
		"`endif", "`ifdef", "`ifndef", "`include", "`line",
		"`nounconnected_drive", "`pragma", "`resetall", "`timescale",
		"`unconnected_drive", "`undef", "`undefineall",
		NULL
	},
};

// .enabled field cannot be shared by two languages
static fieldDefinition VerilogFields[] = {
	{ .name = "parameter",
	  .description = "parameter whose value can be overridden.",
	  .enabled = false,
	  .dataType = FIELDTYPE_BOOL },
};

static fieldDefinition SystemVerilogFields[] = {
	{ .name = "parameter",
	  .description = "parameter whose value can be overridden.",
	  .enabled = false,
	  .dataType = FIELDTYPE_BOOL },
};

/*
 *   PROTOTYPE DEFINITIONS
 */

static bool findBlockName (tokenInfo *const token);
static void processDefine (tokenInfo *const token);
static bool readWordToken (tokenInfo *const token, int c);
static void tagNameList (tokenInfo* token, int c);
static void updateKind (tokenInfo *const token);

/*
 *   FUNCTION DEFINITIONS
 */

static short isContainer (verilogKind kind)
{
	switch (kind)
	{
		case K_MODULE:
		case K_TASK:
		case K_FUNCTION:
		case K_BLOCK:
		case K_CLASS:
		case K_COVERGROUP:
		case K_INTERFACE:
		case K_PACKAGE:
		case K_PROGRAM:
		case K_PROPERTY:
		case K_TYPEDEF:
		case K_ENUM:
			return true;
		default:
			return false;
	}
}

static short isTempContext (tokenInfo const* token)
{
	switch (token->kind)
	{
		case K_TYPEDEF:
		case K_ENUM:
			return true;
		default:
			return false;
	}
}

static short hasSimplePortList (verilogKind kind)
{
	switch (kind)
	{
		case K_TASK:
		case K_FUNCTION:
		case K_CLASS:
		case K_INTERFACE:
		case K_PROGRAM:
		case K_PROPERTY:
			return true;
		default:
			return false;
	}
}

static void clearToken (tokenInfo *token)
{
	token->kind = K_UNDEFINED;	// to be set by updateKind()
	vStringClear (token->name);
	token->lineNumber = getInputLineNumber ();
	token->filePosition = getInputFilePosition ();
	token->scope = NULL;
	token->nestLevel = 0;
	token->lastKind = K_UNDEFINED;
	vStringClear (token->blockName);
	vStringClear (token->inheritance);
	token->prototype = false;
	token->classScope = false;
	token->parameter = false;
	token->hasParamList = false;
}

static tokenInfo *newToken (void)
{
	tokenInfo *const token = xMalloc (1, tokenInfo);
	token->name = vStringNew ();
	token->blockName = vStringNew ();
	token->inheritance = vStringNew ();
	clearToken(token);
	return token;
}

static tokenInfo *dupToken (tokenInfo *token)
{
	tokenInfo *dup = newToken ();
	tokenInfo tmp = *dup;	// save vStrings, name, blockName, and inheritance
	*dup = *token;
	// revert vStrings allocated for dup
	dup->name = tmp.name;
	dup->blockName = tmp.blockName;
	dup->inheritance = tmp.inheritance;
	// copy contents of vStrings
	vStringCopy (dup->name, token->name);
	vStringCopy (dup->blockName, token->blockName);
	vStringCopy (dup->inheritance, token->inheritance);
	return dup;
}

static void deleteToken (tokenInfo * const token)
{
	if (token != NULL)
	{
		vStringDelete (token->name);
		vStringDelete (token->blockName);
		vStringDelete (token->inheritance);
		eFree (token);
	}
}

static tokenInfo *pushToken (tokenInfo * const token, tokenInfo * const tokenPush)
{
	tokenPush->scope = token;
	return tokenPush;
}

static tokenInfo *appendToken (tokenInfo * const token, tokenInfo * const tokenAppend)
{
	token->scope = tokenAppend;
	return tokenAppend;
}

static tokenInfo *popToken (tokenInfo * const token)
{
	tokenInfo *localToken;
	if (token != NULL)
	{
		localToken = token->scope;
		deleteToken (token);
		return localToken;
	}
	return NULL;
}

static void pruneTokens (tokenInfo * token)
{
	while ((token = popToken (token)));
}

static const char *getNameForKind (const verilogKind kind)
{
	if (isInputLanguage (Lang_systemverilog))
		return (SystemVerilogKinds[kind]).name;
	else /* isInputLanguage (Lang_verilog) */
		return (VerilogKinds[kind]).name;
}

static char kindEnabled (const verilogKind kind)
{
	if (isInputLanguage (Lang_systemverilog))
		return SystemVerilogKinds[kind].enabled;
	else /* isInputLanguage (Lang_verilog) */
		return VerilogKinds[kind].enabled;
}

static void buildKeywordHash (const langType language, unsigned int idx)
{
	size_t i;
	const size_t count = ARRAY_SIZE (KeywordTable);
	for (i = 0  ;  i < count  ;  ++i)
	{
		const keywordAssoc *p = &KeywordTable [i];
		if (p->isValid [idx])
			addKeyword (p->keyword, language, (int) p->kind);
	}
}

static void initializeVerilog (const langType language)
{
	Lang_verilog = language;
	buildKeywordHash (language, IDX_VERILOG);
	addKeywordGroup (&verilogKeywords, language);
	addKeywordGroup (&verilogDirectives, language);
}

static void initializeSystemVerilog (const langType language)
{
	Lang_systemverilog = language;
	buildKeywordHash (language, IDX_SYSTEMVERILOG);
	addKeywordGroup (&systemVerilogKeywords, language);
	addKeywordGroup (&systemVerilogDirectives, language);
}

static void vUngetc (int c)
{
	Assert (Ungetc == '\0');
	Ungetc = c;
}

/* Mostly copied from cppSkipOverCComment() in cpreprocessor.c.
 *
 * cppSkipOverCComment() uses the internal ungetc buffer of
 * CPreProcessor.  On the other hand, the Verilog parser uses
 * getcFromInputFile() directly. getcFromInputFile() uses just
 * another internal ungetc buffer. Using them mixed way will
 * cause a trouble. */
static int verilogSkipOverCComment (void)
{
	int c =  getcFromInputFile();

	while (c != EOF)
	{
		if (c != '*')
			c = getcFromInputFile ();
		else
		{
			const int next = getcFromInputFile ();

			if (next != '/')
				c = next;
			else
			{
				c = SPACE;  /* replace comment with space */
				break;
			}
		}
	}
	return c;
}

static int vGetc (void)
{
	int c;
	if (Ungetc == '\0')
		c = getcFromInputFile ();
	else
	{
		c = Ungetc;
		Ungetc = '\0';
	}
	if (c == '/')
	{
		int c2 = getcFromInputFile ();
		if (c2 == EOF)
			return EOF;
		else if (c2 == '/')  /* strip comment until end-of-line */
		{
			do
				c = getcFromInputFile ();
			while (c != '\n'  &&  c != EOF);
		}
		else if (c2 == '*')  /* strip block comment */
		{
			c = verilogSkipOverCComment();
		}
		else
		{
			ungetcToInputFile (c2);
		}
	}
	else if (c == '"')  /* strip string contents */
	{
		int c2;
		do
			c2 = getcFromInputFile ();
		while (c2 != '"'  &&  c2 != EOF);
		c = '@';
	}
	return c;
}

// [a-zA-Z_`]
static bool isFirstIdentifierCharacter (const int c)
{
	return (isalpha (c) || c == '_' || c == '`');
}

// [a-zA-Z0-9_`$]
static bool isIdentifierCharacter (const int c)
{
	return (isalnum (c) || c == '_' || c == '`' || c == '$');
}

static int skipWhite (int c)
{
	while (isspace (c))
		c = vGetc ();
	return c;
}

static int skipPastMatch (const char *const pair)
{
	const int begin = pair [0], end = pair [1];
	int matchLevel = 1;
	int c;
	do
	{
		c = vGetc ();
		if (c == begin)
			++matchLevel;
		else if (c == end)
			--matchLevel;
	}
	while (c != EOF && matchLevel > 0);
	return vGetc ();
}

static int skipDimension (int c)
{
	while (c == '[')
	{
		c = skipWhite (skipPastMatch ("[]"));
	}
	return c;
}

static int skipToSemiColon (void)
{
	int c;
	do
	{
		c = vGetc ();
	} while (c != EOF && c != ';');
	return c;	// ';' or EOF
}

static int skipExpression(int c)
{
	while (c != EOF && c != ','  &&  c != ';' && c != ')' && c != '}' && c != ']')
	{
		if (c == '(')
			c = skipPastMatch ("()");
		else if (c == '{')
			c = skipPastMatch ("{}");
		else if (c == '[')
			c = skipPastMatch ("[]");
		else
			c = skipWhite (vGetc ());
	}
	return c;
}

// Skip to newline. The newline preceded by a backslash ( \ ) is ignored.
static void skipToNewLine ()
{
	int c;
	bool escape = false;
	while (true)
	{
		c = vGetc ();
		if ((c == '\n' && ! escape) ||  c == EOF)
			break;
		escape = (c == '\\');
	}
}

static int skipMacro (int c)
{
	if (c == '`')
	{
		tokenInfo *token = newToken ();	// don't update token outside

		readWordToken (token, c);
		updateKind (token);
		/* Skip compiler directive other than `define */
		if (token->kind == K_DIRECTIVE)
		{
			skipToNewLine();
			c = skipWhite (vGetc ());
		}
		/* Skip `define */
		else if (token->kind == K_DEFINE)
		{
			processDefine(token);
			c = skipWhite (vGetc ());
		}
		/* Skip macro or macro functions */
		else
		{
			c = skipWhite (vGetc ());
			if (c == '(')
			{
				c = skipPastMatch ("()");
				c = skipWhite (c);
			}
		}
		deleteToken (token);
	}
	return c;
}

/* read an identifier, keyword, number, compiler directive, or macro identifier */
static bool readWordToken (tokenInfo *const token, int c)
{
	if (isFirstIdentifierCharacter (c))
	{
		clearToken (token);
		do
		{
			vStringPut (token->name, c);
			c = vGetc ();
		} while (isIdentifierCharacter (c));
		vUngetc (c);
		return true;
	}
	return false;
}

/* check if an identifier:
 *   simple_identifier ::= [ a-zA-Z_ ] { [ a-zA-Z0-9_$ ] } */
static bool isIdentifier (tokenInfo* token)
{
	if (token->kind == K_UNDEFINED)
	{
		for (int i = 0; i < vStringLength (token->name); i++)
		{
			int c = vStringChar (token->name, i);
			if (i == 0)
			{
				if (c == '`' || !isFirstIdentifierCharacter (c))
					return false;
			}
			else
			{
				if (!isIdentifierCharacter (c))
					return false;
			}
		}
		return true;
	}
	else
		return false;
}

static verilogKind getKindForToken (tokenInfo *const token)
{
	return (verilogKind) lookupKeyword (vStringValue (token->name), getInputLanguage () );
}

static void updateKind (tokenInfo *const token)
{
	verilogKind kind = getKindForToken (token);
	token->kind = ((kind == K_UNDEFINED) && isIdentifier(token)) ? K_IDENTIFIER : kind;
}

static void createContext (verilogKind kind, vString* const name)
{
	tokenInfo *const scope = newToken ();
	vStringCopy (scope->name, name);
	scope->kind = kind;

	if (scope)
	{
		vString *contextName = vStringNew ();

		/* Determine full context name */
		if (currentContext->kind != K_UNDEFINED)
		{
			vStringCopy (contextName, currentContext->name);
			vStringPut (contextName, '.');
		}
		vStringCat (contextName, scope->name);
		/* Create context */
		currentContext = pushToken (currentContext, scope);
		vStringCopy (currentContext->name, contextName);
		vStringDelete (contextName);
		verbose ("Created new context %s (kind %d)\n", vStringValue (currentContext->name), currentContext->kind);
	}
}

static void dropContext ()
{
	verbose ("Dropping context %s\n", vStringValue (currentContext->name));
	currentContext = popToken (currentContext);
}

static void dropEndContext (tokenInfo *const token)
{
	verbose ("current context %s; context kind %0d; nest level %0d\n", vStringValue (currentContext->name), currentContext->kind, currentContext->nestLevel);
	if ((currentContext->kind == K_COVERGROUP && strcmp (vStringValue (token->name), "endgroup") == 0) ||
	    (currentContext->kind == K_BLOCK && currentContext->nestLevel == 0 && token->kind == K_END)
	    )
	{
		dropContext ();
		findBlockName (token);
	}
	else
	{
		vString *endTokenName = vStringNewInit("end");
		vStringCatS (endTokenName, getNameForKind (currentContext->kind));
		if (strcmp (vStringValue (token->name), vStringValue (endTokenName)) == 0)
		{
			dropContext ();
			findBlockName (token);
			if (currentContext->classScope)
			{
				verbose ("Dropping local context %s\n", vStringValue (currentContext->name));
				currentContext = popToken (currentContext);
			}
		}
		vStringDelete(endTokenName);
	}
}


static void createTag (tokenInfo *const token, verilogKind kind)
{
	tagEntryInfo tag;

	/* FIXME: This if-clause should be removed. */
	if (kind == K_UNDEFINED || kind == K_IDENTIFIER)
	{
		verbose ("Unexpected token kind %d\n", kind);
		return;
	}

	if (kind == K_LOCALPARAM)
	{
		kind = K_CONSTANT;
	}
	else if (kind == K_PARAMETER)
	{
		kind = K_CONSTANT;
		// See LRM 2017 6.20.1 Parameter declaration syntax
		if (currentContext->kind != K_CLASS && currentContext->kind != K_PACKAGE && !currentContext->hasParamList)
			token->parameter = true;
	}
	Assert (kind >= 0);

	/* check if a container before kind is modified by prototype */
	/* BTW should we create a context for a prototype? */
	bool container = isContainer(kind);

	/* Determine if kind is prototype */
	if (currentContext->prototype)
	{
		kind = K_PROTOTYPE;
	}

	/* Do nothing it tag name is empty or tag kind is disabled */
	if (vStringLength (token->name) == 0)
	{
		verbose ("Unexpected empty token\n");	/* FIXME: uncovered */
		return;
	}
	if (! kindEnabled (kind))
	{
		verbose ("kind disabled\n");
		return;
	}

	/* Create tag */
	initTagEntry (&tag,
		      vStringValue (token->name),
		      kind);
	tag.lineNumber = token->lineNumber;
	tag.filePosition = token->filePosition;

	verbose ("Adding tag %s (kind %d)", vStringValue (token->name), kind);
	if (currentContext->kind != K_UNDEFINED)
	{
		verbose (" to context %s\n", vStringValue (currentContext->name));
		currentContext->lastKind = kind;
		tag.extensionFields.scopeKindIndex = currentContext->kind;
		tag.extensionFields.scopeName = vStringValue (currentContext->name);
	}
	verbose ("\n");
	if (vStringLength (token->inheritance) > 0)
	{
		tag.extensionFields.inheritance = vStringValue (token->inheritance);
		verbose ("Class %s extends %s\n", vStringValue (token->name), tag.extensionFields.inheritance);
	}

	if (token->parameter)
		attachParserField (&tag, false,
						   fieldTable [F_PARAMETER].ftype, "");

	makeTagEntry (&tag);

	if (isXtagEnabled(XTAG_QUALIFIED_TAGS) && currentContext->kind != K_UNDEFINED)
	{
		vString *const scopedName = vStringNew ();

		vStringCopy (scopedName, currentContext->name);
		vStringPut (scopedName, '.');
		vStringCat (scopedName, token->name);
		tag.name = vStringValue (scopedName);

		markTagExtraBit (&tag, XTAG_QUALIFIED_TAGS);
		makeTagEntry (&tag);

		vStringDelete (scopedName);
	}

	/* Push token as context if it is a container */
	if (container)
	{
		createContext (kind, token->name);

		/* Include found contents in context */
		if (tagContents != NULL)
		{
			tokenInfo* content = tagContents;

			verbose ("Including tagContents\n");
			do
			{
				createTag (content, content->kind);
				content = content->scope;
			} while (content);
		}

		/* Drop temporary contexts */
		if (isTempContext (currentContext))
		{
			dropContext ();
		}
	}

	/* Clear no longer required inheritance information */
	vStringClear (token->inheritance);
}

static bool findBlockName (tokenInfo *const token)
{
	int c;

	c = skipWhite (vGetc ());
	if (c == ':')
	{
		c = skipWhite (vGetc ());
		return readWordToken (token, c);
	}
	else
		vUngetc (c);
	return false;
}

static void processBegin (tokenInfo *const token)
{
	currentContext->nestLevel++;
	if (findBlockName (token))
	{
		verbose ("Found block: %s\n", vStringValue (token->name));
		createTag (token, K_BLOCK);
		verbose ("Current context %s\n", vStringValue (currentContext->name));
	}
}

static void processEnd (tokenInfo *const token)
{
	currentContext->nestLevel--;
	if (findBlockName (token))
	{
		verbose ("Found block: %s\n", vStringValue (token->name));
		if (currentContext->kind == K_BLOCK && currentContext->nestLevel <= 1)
			dropContext ();	/* FIXME: uncovered */
	}
}

static void processPortList (tokenInfo *token, int c)
{
	if ((c = skipWhite (c)) == '(')
	{
		/* Get next non-whitespace character after ( */
		c = skipWhite (vGetc ());

		while (c != ';' && c != EOF)
		{
			c = skipDimension (c);
			if (c == '(')
			{
				c = skipPastMatch ("()");
			}
			else if (c == '{')
			{
				c = skipPastMatch ("{}");
			}
			else if (c == '`')
			{
				c = skipMacro (c);
			}
			else if (c == '=')
			{
				/* Search for next port or end of port declaration */
				while (c != ',' && c != ')' && c != EOF)
				{
					c = skipWhite (vGetc ());
				}
			}
			else if (readWordToken (token, c))
			{
				updateKind (token);
				if (token->kind == K_IDENTIFIER)
				{
					/* Only add port name if it is the last keyword.
					 * First keyword can be a dynamic type, like a class name */
					c = skipWhite (vGetc ());
					if (! isFirstIdentifierCharacter (c) || c == '`')
					{
						verbose ("Found port: %s\n", vStringValue (token->name));
						createTag (token, K_PORT);
					}
				}
				else
				{
					c = skipWhite (vGetc ());
				}
			}
			else
			{
				c = skipWhite (vGetc ());
			}
		}
		if (c != ';')
			verbose ("Unexpected char c = %c\n", c);
	}

	if (c != EOF)
		vUngetc (c);
}

static int skipParameterAssignment (int c)
{
	if (c == '#')
	{
		c = skipWhite (vGetc ());
		if (c == '(')
			c = skipWhite (skipPastMatch ("()"));
	}
	return c;
}

/* Functions are treated differently because they may also include the
 * type of the return value.
 * Tasks are treated in the same way, although not having a return
 * value.*/
static void processFunction (tokenInfo *const token)
{
	verilogKind kind = token->kind;	// K_FUNCTION or K_TASK
	int c;

	/* Search for function name
	 * Last identifier found before a '(' or a ';' is the function name */
	c = skipWhite (vGetc ());
	do
	{
		readWordToken (token, c);
		c = skipWhite (vGetc ());
		/* skip parameter assignment of a class type
		 *    ex. function uvm_port_base #(IF) get_if(int index=0); */
		c = skipParameterAssignment (c);

		/* Identify class type prefixes and create respective context*/
		if (isInputLanguage (Lang_systemverilog) && c == ':')
		{
			c = vGetc ();
			if (c == ':')
			{
				verbose ("Found function declaration with class type %s\n", vStringValue (token->name));
				createContext (K_CLASS, token->name);
				currentContext->classScope = true;
			}
			else
			{
				vUngetc (c);
			}
		}
	} while (c != '(' && c != ';' && c != EOF);

	if ( vStringLength (token->name) > 0 )
	{
		verbose ("Found function: %s\n", vStringValue (token->name));

		/* Create tag */
		createTag (token, kind);

		/* Get port list from function */
		processPortList (token, c);
	}
}

static void processEnum (tokenInfo *const token)
{
	int c;
	tokenInfo* enumToken = dupToken (token);

	/* Read enum type */
	c = skipWhite (vGetc ());
	if (readWordToken (token, c))
	{
		tokenInfo* typeQueue = NULL;

		do
		{
			updateKind (token);
			typeQueue = pushToken (typeQueue, dupToken (token));
			verbose ("Enum type %s\n", vStringValue (token->name));
			c = skipWhite (vGetc ());
		} while (readWordToken (token, c));

		/* Undefined kind means that we've reached the end of the
		 * declaration without having any contents defined, which
		 * indicates that this is in fact a forward declaration */
		if (token->kind == K_IDENTIFIER && (typeQueue->scope == NULL || typeQueue->scope->kind != K_UNDEFINED))
		{
			verbose ("Prototype enum found \"%s\"\n", vStringValue (token->name));
			createTag (token, K_PROTOTYPE);
			pruneTokens (typeQueue);
			deleteToken (enumToken);
			return;
		}

		/* Cleanup type queue */
		pruneTokens (typeQueue);
	}

	/* Skip bus width definition */
	c = skipDimension (c);

	/* Search enum elements */
	if (c == '{')
	{
		c = skipWhite (vGetc ());
		while (readWordToken (token, c))
		{
			token->kind = K_CONSTANT;
			tagContents = pushToken (tagContents, dupToken (token));
			verbose ("Pushed enum element \"%s\"\n", vStringValue (token->name));

			/* Skip element ranges */
			/* TODO Implement element ranges */
			c = skipDimension (skipWhite (vGetc ()));

			/* Skip value assignments */
			if (c == '=')
			{
				while (c != '}' && c != ',' && c != EOF)
				{
					c = skipWhite (vGetc ());

					/* Skip enum value concatenations */
					if (c == '{')
					{
						c = skipWhite (skipPastMatch ("{}"));
					}
				}
			}
			/* Skip comma */
			if (c == ',')
			{
				c = skipWhite (vGetc ());
			}
			/* End of enum elements list */
			if (c == '}')
			{
				c = skipWhite (vGetc ());
				break;
			}
		}
	}

	/* Following identifiers are tag names */
	verbose ("Find enum tags. Token %s kind %d\n", vStringValue (enumToken->name), enumToken->kind);
	tagNameList (enumToken, c);
	deleteToken (enumToken);
}

static void processStruct (tokenInfo *const token)
{
	verilogKind kind = token->kind;	// K_STRUCT or K_TYPEDEF
	int c;

	c = skipWhite (vGetc ());

	/* Skip packed, signed, and unsigned */
	while (readWordToken (token, c))
	{
		c = skipWhite (vGetc ());
	}

	/* Skip struct contents */
	if (c == '{')
	{
		c = skipWhite (skipPastMatch ("{}"));
	}
	else
	{
		verbose ("Prototype struct found \"%s\"\n", vStringValue (token->name));
		createTag (token, K_PROTOTYPE);
		return;
	}

	/* Skip packed_dimension */
	c = skipDimension (c);

	/* Following identifiers are tag names */
	verbose ("Find struct|union tags. Token %s kind %d\n", vStringValue (token->name), token->kind);
	token->kind = kind;
	tagNameList (token, c);
}

static void processTypedef (tokenInfo *const token)
{
	int c;

	/* Get typedef type */
	c = skipWhite (vGetc ());
	if (readWordToken (token, c))
	{
		updateKind (token);

		switch (token->kind)
		{
			case K_INTERFACE:
				/* Expecting `typedef interface class` */
				c = skipWhite (vGetc ());
				readWordToken (token, c);
				updateKind (token);
			case K_CLASS:
				/* A typedef class is just a prototype */
				currentContext->prototype = true;
				break;
			case K_ENUM:
				/* Call enum processing function */
				token->kind = K_TYPEDEF;
				processEnum (token);
				return;
			case K_STRUCT:
				/* Call enum processing function */
				token->kind = K_TYPEDEF;
				processStruct (token);
				return;
			default :
				break;
		}

		c = skipWhite (vGetc ());
	}

	/* Skip signed or unsiged */
	if (readWordToken (token, c))
		c = skipWhite (vGetc ());

	/* Skip bus width definition */
	c = skipDimension (c);

	/* Skip remaining identifiers */
	while (readWordToken (token, c))
		c = skipWhite (vGetc ());

	/* Skip past class parameter override */
	c = skipParameterAssignment (c);

	/* Read typedef name */
	if (readWordToken (token, c))
		; // just read a word token
	else
	{
		vUngetc (c);

		/* Empty typedefs are forward declarations and are considered
		 * prototypes */
		if (token->kind == K_IDENTIFIER)
		{
			currentContext->prototype = true;
		}
	}

	/* Use last identifier to create tag, but always with kind typedef */
	createTag (token, K_TYPEDEF);
}

static tokenInfo * processParameterList (tokenInfo *token, int c)
{
	tokenInfo *head = NULL;
	tokenInfo *parameters = NULL;
	bool parameter = true;	// default "parameter"
	if (c == '#')
	{
		c = skipWhite (vGetc ());
		if (c == '(')
		{
			do
			{
				c = skipWhite (vGetc ());
				if (readWordToken (token, c))
				{
					updateKind (token);
					verbose ("Found parameter %s\n", vStringValue (token->name));
					if (token->kind == K_IDENTIFIER)
					{
						c = skipWhite (vGetc ());
						if (c == ',' || c == ')' || c == '=')	// ignore user defined type
						{
							tokenInfo *param = dupToken (token);
							param->kind = K_CONSTANT;
							param->parameter = parameter;
							if (head == NULL)
							{
								head = param;
								parameters = param;
							} else
								parameters = appendToken (parameters, param);	// append token on parameters

							c = skipExpression (c);
						}
					}
					else if (token->kind == K_PARAMETER)
						parameter = true;
					else if (token->kind == K_LOCALPARAM)
						parameter = false;
				}
				else if (c == '[') {
					c =skipDimension(c);
					vUngetc (c);
				}
			} while (c != ')' && c != EOF);
			c = skipWhite (vGetc ());
		}
	}
	vUngetc (c);
	return head;
}

static void processClass (tokenInfo *const token)
{
	int c;
	tokenInfo *classToken;
	tokenInfo *parameters;

	/* Get identifiers */
	c = skipWhite (vGetc ());
	if (!readWordToken (token, c))
	{
		verbose ("Unexpected input: class name is expected.\n");
		return;
	}

	/* save token */
	classToken = dupToken (token);
	c = skipWhite (vGetc ());

	/* Find class parameters list */
	parameters = processParameterList (token, c);
	c = skipWhite (vGetc ());

	/* Search for inheritance information */
	if (readWordToken (token, c))
	{
		if (strcmp (vStringValue (token->name), "extends") == 0)
		{
			c = skipWhite (vGetc ());
			readWordToken (token, c);
			vStringCopy (classToken->inheritance, token->name);
			verbose ("Inheritance %s\n", vStringValue (classToken->inheritance));
		}
	}

	createTag (classToken, K_CLASS);
	deleteToken (classToken);

	/* Add parameter list */
	while (parameters)
	{
		createTag (parameters, K_CONSTANT);
		parameters = popToken (parameters);
	}
}

static void processDefine (tokenInfo *const token)
{
	/* Bug #961001: Verilog compiler directives are line-based. */
	int c = skipWhite (vGetc ());
	readWordToken (token, c);
	createTag (token, K_CONSTANT);
	skipToNewLine ();
}

static void processAssertion (tokenInfo *const token)
{
	if (vStringLength (currentContext->blockName) > 0)
	{
		int c;
		vStringCopy (token->name, currentContext->blockName);
		createTag (token, K_ASSERTION);
		c = skipToSemiColon ();
		vUngetc (c);
	}
}

/* covergroup, interface, modport, module, package, program, property */
static void processDesignElement (tokenInfo *const token)
{
	verilogKind kind = token->kind;
	int c = skipWhite (vGetc ());

	if (readWordToken (token, c))
	{
		while (getKindForToken (token) == K_IGNORE)
		{
			c = skipWhite (vGetc ());
			readWordToken (token, c);
		}
		createTag (token, kind);

		c = skipWhite (vGetc ());
		if (c == '#')
		{
			tokenInfo *parameters = processParameterList (token, c);
			while (parameters)
			{
				createTag (parameters, K_CONSTANT);
				parameters = popToken (parameters);
			}
			// disable parameter property on parameter declaration statement
			currentContext->hasParamList = true;
			c = skipWhite (vGetc ());
		}

		/* Get port list if required */
		if (c == '(')
		{
			if (kind == K_MODPORT)
				c = skipPastMatch ("()");	// ignore port list
			else if (hasSimplePortList (kind))
				processPortList (token, c);
		}
		else
		{
			vUngetc (c);
		}
	}
}

static int skipDelay(tokenInfo* token, int c)
{
	if (c == '#')
	{
		c = skipWhite (vGetc ());
		if (c == '(')
			c = skipPastMatch ("()");
		else if (c == ('#')) {
			c = skipToSemiColon ();	// a dirty hack for "x ##delay1 y[*min:max];"
		}
		else	// time literals
		{
			while (isIdentifierCharacter (c) || c == '.')
				c = vGetc ();
		}
		c = skipWhite (c);
	}
	return c;
}

static void tagNameList (tokenInfo* token, int c)
{
	verilogKind kind = token->kind;
	verilogKind actualKind = K_UNDEFINED;
	bool repeat;

	/* Many keywords can have bit width.
	*   reg [3:0] net_name;
	*   inout [(`DBUSWIDTH-1):0] databus;
	*/
	// skip drive|charge strength or type_reference
	if (c == '(')
		c = skipPastMatch ("()");
	c = skipDimension (skipWhite (c));
	c = skipDelay(token, c);

	do
	{
		repeat = false;

		while (c == '`')
			c = skipMacro (c);

		if (readWordToken (token, c))
		{
			updateKind (token);
			if (kind == K_IDENTIFIER)	// user defined type
			{
				if (token->kind == K_NET)
				{
					actualKind = K_NET;
					repeat = true;
				}
				else if (token->kind == K_REGISTER)
				{
					actualKind = K_REGISTER;
					repeat = true;
				} else {	// identifier of a user defined type
					kind = K_REGISTER;	// !!!FIXME: consider kind of the user defined type
				}
			}
			else if ((token->kind != K_IDENTIFIER) // skip keywords or an identifier on port
					 || ((kind == K_PORT) && (token->kind == K_IDENTIFIER)))
					repeat = true;
		}
		c = skipWhite (vGetc ());

		// skip unpacked dimension (or packed dimension after type-words)
		c = skipDimension (skipWhite (c));
		if (c == ',' || c == ';' || c == ')')
		{
			createTag (token, kind == K_UNDEFINED ? actualKind : kind);
			repeat = false;
		}
		else if (c == '=')
		{
			if (!repeat)	// ignore procedual assignment: foo = bar;
				createTag (token, kind == K_UNDEFINED ? actualKind : kind);

			c = skipExpression (skipWhite (vGetc ()));
		}
		if (c == ',')
		{
			c = skipWhite (vGetc ());
			repeat = true;
		}
	} while (repeat);
	/* skip port list of module instance: foo bar(xx, yy); */
	if (c == '(')
		c = skipPastMatch ("()");
	vUngetc (c);
}

static void findTag (tokenInfo *const token)
{
	verbose ("Checking token %s of kind %d\n", vStringValue (token->name), token->kind);

	if (currentContext->kind != K_UNDEFINED && (token->kind == K_END || token->kind == K_END_DE))
	{
		/* Drop context, but only if an end token is found */
		dropEndContext (token);
	}

	switch (token->kind)
	{
		case K_CONSTANT:
		case K_EVENT:
		case K_LOCALPARAM:
		case K_NET:
		case K_PARAMETER:
		case K_PORT:
		case K_REGISTER:
			tagNameList (token, skipWhite (vGetc ()));
			break;
		case K_IDENTIFIER:
			{
				int c = skipWhite(vGetc());
				if (c == ':')
					vUngetc(c); /* label */
				else if (c == '=')
					c = skipExpression (skipWhite(vGetc()));
				else
					tagNameList(token, c); /* user defined type */
			}
			break;
		case K_CLASS:
			processClass(token);
			break;
		case K_TYPEDEF:
			processTypedef(token);
			break;
		case K_ENUM:
			processEnum(token);
			break;
		case K_STRUCT:
			processStruct(token);
			break;
		case K_PROTOTYPE:
			currentContext->prototype = true;
			break;

		case K_COVERGROUP:
		case K_INTERFACE:
		case K_MODPORT:
		case K_MODULE:
		case K_PACKAGE:
		case K_PROGRAM:
		case K_PROPERTY:
			processDesignElement(token);
			break;
		case K_BEGIN:
			processBegin(token);
			break;
		case K_END:
			processEnd(token);
			break;
		case K_FUNCTION:
		case K_TASK:
			processFunction(token);
			break;
		case K_ASSERTION:
			processAssertion(token);
			break;

		case K_DEFINE:
			processDefine(token);
			break;
		case K_DIRECTIVE:
			skipToNewLine();
			break;

		case K_END_DE:
		case K_IGNORE:
			return;
		default:
			verbose ("Unexpected kind->token %d\n", token->kind);
	}
}

static void findVerilogTags (void)
{
	tokenInfo *const token = newToken ();
	int c = '\0';
	currentContext = newToken ();
	fieldTable = isInputLanguage (Lang_verilog) ? VerilogFields : SystemVerilogFields;

	while (c != EOF)
	{
		c = skipWhite (vGetc ());
		switch (c)
		{
			/* Store current block name whenever a : is found
			 * This is used later by any tag type that requires this information
			 * */
			case ':':
				vStringCopy (currentContext->blockName, token->name);
				break;
			case ';':
				/* Drop context on prototypes because they don't have an
				 * end statement */
				if (currentContext->scope && currentContext->scope->prototype)
				{
					dropContext ();
				}
				/* Prototypes end at the end of statement */
				currentContext->prototype = false;

				/* Cleanup tag contents list at end of declaration */
				while (tagContents)
				{
					tagContents = popToken (tagContents);
				}
				break;
			case '#':
				c = skipDelay (token, c);
				break;
			default :
				if (readWordToken (token, c))
				{
					updateKind (token);
					if (token->kind != K_UNDEFINED)
						findTag (token);
				}
		}
	}

	deleteToken (token);
	pruneTokens (currentContext);
	currentContext = NULL;
}

extern parserDefinition* VerilogParser (void)
{
	static const char *const extensions [] = { "v", NULL };
	parserDefinition* def = parserNew ("Verilog");
	def->kindTable      = VerilogKinds;
	def->kindCount  = ARRAY_SIZE (VerilogKinds);
	def->fieldTable = VerilogFields;
	def->fieldCount = ARRAY_SIZE (VerilogFields);
	def->extensions = extensions;
	def->parser     = findVerilogTags;
	def->initialize = initializeVerilog;
	return def;
}

extern parserDefinition* SystemVerilogParser (void)
{
	static const char *const extensions [] = { "sv", "svh", "svi", NULL };
	parserDefinition* def = parserNew ("SystemVerilog");
	def->kindTable      = SystemVerilogKinds;
	def->kindCount  = ARRAY_SIZE (SystemVerilogKinds);
	def->fieldTable = SystemVerilogFields;
	def->fieldCount = ARRAY_SIZE (SystemVerilogFields);
	def->extensions = extensions;
	def->parser     = findVerilogTags;
	def->initialize = initializeSystemVerilog;
	return def;
}
