%{
#include <string.h>
#ifdef __x86_64__
#define YYSTYPE_IS_DECLARED 1
typedef long long YYSTYPE;
#endif
#include "parser.tab.h"
%}
%%
hostnm           { return (T_HOSTNM); }
portnm           { return (T_PORTNM); }
[0-9]+           { yylval = (YYSTYPE) atoi(yytext); return (T_NUM); }
[a-zA-Z0-9\/.-]+ { yylval = (YYSTYPE) strdup(yytext); return (T_STRING); }
=                { return (L_EQ); }
\"               { return (L_QUOTE); }
\{               { return (L_OPEN_PAREN); }
\}               { return (L_CLOSE_PAREN); }
;                { return (L_SEMICOLON); }
\n               { }	/* 改行を無視するため、return は書かない */
[ \t]+           { }	/* スペースやタブを無視するため、return は書かない */
%%
