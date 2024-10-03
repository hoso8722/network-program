%{
#include <stdio.h>
extern int yylex(void);
extern FILE *yyin;
extern char g_program_name[80];
extern char g_hostnm[80];
extern char g_portnm[80];
#ifdef __x86_64__
#define YYSTYPE_IS_DECLARED 1
typedef long long YYSTYPE;
#endif
int
yywrap(void)
{
    return (1);
}
void
yyerror(const char *msg)
{
    (void) fprintf(stderr, "%s\n", msg);
}
%}
%token	T_HOSTNM
%token	T_PORTNM
%token	T_STRING
%token	T_NUM
%token  L_EQ
%token	L_QUOTE
%token	L_OPEN_PAREN
%token	L_CLOSE_PAREN
%token	L_SEMICOLON
%%
program_name
    : T_STRING L_OPEN_PAREN statements L_CLOSE_PAREN {
        (void) snprintf(g_program_name,
                        sizeof(g_program_name),
                        "%s",
                        (char *) $1);
        free((char *) $1);
    }
    ;

statement
    : hostnm_statement
    | portnm_statement
    ;

statements
    : statement
    | statements statement
    ;

hostnm_statement
    : T_HOSTNM L_EQ L_QUOTE T_STRING L_QUOTE L_SEMICOLON {
        (void) snprintf(g_hostnm,
                        sizeof(g_hostnm),
                        "%s",
                        (char *) $4);
        free((char *) $4);
    }
    ;

portnm_statement
    : T_PORTNM L_EQ T_NUM L_SEMICOLON {
        (void) snprintf(g_portnm,
                        sizeof(g_portnm),
                        "%d",
                        (int) $3);
        }
    | T_PORTNM L_EQ L_QUOTE T_STRING L_QUOTE L_SEMICOLON {
        (void) snprintf(g_portnm,
                        sizeof(g_portnm),
                        "%s",
                        (char *) $4);
        free((char *) $4);
	}
    ;
