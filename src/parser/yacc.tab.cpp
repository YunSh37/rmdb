/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

#line 86 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "yacc.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_SHOW = 3,                       /* SHOW  */
  YYSYMBOL_TABLES = 4,                     /* TABLES  */
  YYSYMBOL_CREATE = 5,                     /* CREATE  */
  YYSYMBOL_TABLE = 6,                      /* TABLE  */
  YYSYMBOL_DROP = 7,                       /* DROP  */
  YYSYMBOL_DESC = 8,                       /* DESC  */
  YYSYMBOL_INSERT = 9,                     /* INSERT  */
  YYSYMBOL_INTO = 10,                      /* INTO  */
  YYSYMBOL_VALUES = 11,                    /* VALUES  */
  YYSYMBOL_DELETE = 12,                    /* DELETE  */
  YYSYMBOL_FROM = 13,                      /* FROM  */
  YYSYMBOL_ASC = 14,                       /* ASC  */
  YYSYMBOL_ORDER = 15,                     /* ORDER  */
  YYSYMBOL_BY = 16,                        /* BY  */
  YYSYMBOL_WHERE = 17,                     /* WHERE  */
  YYSYMBOL_UPDATE = 18,                    /* UPDATE  */
  YYSYMBOL_SET = 19,                       /* SET  */
  YYSYMBOL_SELECT = 20,                    /* SELECT  */
  YYSYMBOL_INT = 21,                       /* INT  */
  YYSYMBOL_CHAR = 22,                      /* CHAR  */
  YYSYMBOL_FLOAT = 23,                     /* FLOAT  */
  YYSYMBOL_BIGINT = 24,                    /* BIGINT  */
  YYSYMBOL_DATETIME = 25,                  /* DATETIME  */
  YYSYMBOL_INDEX = 26,                     /* INDEX  */
  YYSYMBOL_AND = 27,                       /* AND  */
  YYSYMBOL_JOIN = 28,                      /* JOIN  */
  YYSYMBOL_ON = 29,                        /* ON  */
  YYSYMBOL_EXIT = 30,                      /* EXIT  */
  YYSYMBOL_HELP = 31,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 32,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 33,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 34,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 35,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 36,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 37,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 38,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_EXPLAIN = 39,                   /* EXPLAIN  */
  YYSYMBOL_MAX = 40,                       /* MAX  */
  YYSYMBOL_MIN = 41,                       /* MIN  */
  YYSYMBOL_COUNT = 42,                     /* COUNT  */
  YYSYMBOL_SUM = 43,                       /* SUM  */
  YYSYMBOL_AS = 44,                        /* AS  */
  YYSYMBOL_GROUP = 45,                     /* GROUP  */
  YYSYMBOL_HAVING = 46,                    /* HAVING  */
  YYSYMBOL_LIMIT = 47,                     /* LIMIT  */
  YYSYMBOL_SEMI = 48,                      /* SEMI  */
  YYSYMBOL_STATIC_CHECKPOINT = 49,         /* STATIC_CHECKPOINT  */
  YYSYMBOL_LEQ = 50,                       /* LEQ  */
  YYSYMBOL_NEQ = 51,                       /* NEQ  */
  YYSYMBOL_GEQ = 52,                       /* GEQ  */
  YYSYMBOL_T_EOF = 53,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 54,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 55,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 56,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_BIGINT = 57,              /* VALUE_BIGINT  */
  YYSYMBOL_VALUE_FLOAT = 58,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 59,                /* VALUE_BOOL  */
  YYSYMBOL_60_ = 60,                       /* ';'  */
  YYSYMBOL_61_ = 61,                       /* '='  */
  YYSYMBOL_62_ = 62,                       /* '('  */
  YYSYMBOL_63_ = 63,                       /* ')'  */
  YYSYMBOL_64_ = 64,                       /* ','  */
  YYSYMBOL_65_ = 65,                       /* '.'  */
  YYSYMBOL_66_ = 66,                       /* '<'  */
  YYSYMBOL_67_ = 67,                       /* '>'  */
  YYSYMBOL_68_ = 68,                       /* '*'  */
  YYSYMBOL_YYACCEPT = 69,                  /* $accept  */
  YYSYMBOL_start = 70,                     /* start  */
  YYSYMBOL_stmt = 71,                      /* stmt  */
  YYSYMBOL_txnStmt = 72,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 73,                    /* dbStmt  */
  YYSYMBOL_setStmt = 74,                   /* setStmt  */
  YYSYMBOL_ddl = 75,                       /* ddl  */
  YYSYMBOL_dml = 76,                       /* dml  */
  YYSYMBOL_fieldList = 77,                 /* fieldList  */
  YYSYMBOL_colNameList = 78,               /* colNameList  */
  YYSYMBOL_field = 79,                     /* field  */
  YYSYMBOL_type = 80,                      /* type  */
  YYSYMBOL_valueList = 81,                 /* valueList  */
  YYSYMBOL_value = 82,                     /* value  */
  YYSYMBOL_condition = 83,                 /* condition  */
  YYSYMBOL_optWhereClause = 84,            /* optWhereClause  */
  YYSYMBOL_whereClause = 85,               /* whereClause  */
  YYSYMBOL_col = 86,                       /* col  */
  YYSYMBOL_op = 87,                        /* op  */
  YYSYMBOL_expr = 88,                      /* expr  */
  YYSYMBOL_selColList = 89,                /* selColList  */
  YYSYMBOL_selCol = 90,                    /* selCol  */
  YYSYMBOL_optAlias = 91,                  /* optAlias  */
  YYSYMBOL_aggFunc = 92,                   /* aggFunc  */
  YYSYMBOL_optGroupBy = 93,                /* optGroupBy  */
  YYSYMBOL_optHaving = 94,                 /* optHaving  */
  YYSYMBOL_havingClause = 95,              /* havingClause  */
  YYSYMBOL_havingCondition = 96,           /* havingCondition  */
  YYSYMBOL_optLimit = 97,                  /* optLimit  */
  YYSYMBOL_setClauses = 98,                /* setClauses  */
  YYSYMBOL_setClause = 99,                 /* setClause  */
  YYSYMBOL_selector = 100,                 /* selector  */
  YYSYMBOL_tableRef = 101,                 /* tableRef  */
  YYSYMBOL_optOnClause = 102,              /* optOnClause  */
  YYSYMBOL_fromList = 103,                 /* fromList  */
  YYSYMBOL_opt_order_clause = 104,         /* opt_order_clause  */
  YYSYMBOL_order_clause = 105,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 106,             /* opt_asc_desc  */
  YYSYMBOL_set_knob_type = 107,            /* set_knob_type  */
  YYSYMBOL_tbName = 108,                   /* tbName  */
  YYSYMBOL_colName = 109                   /* colName  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  54
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   196

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  69
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  41
/* YYNRULES -- Number of rules.  */
#define YYNRULES  105
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  207

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   314


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      62,    63,    68,     2,    64,     2,    65,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    60,
      66,    61,    67,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    69,    69,    74,    79,    84,    92,    93,    94,    95,
      96,   100,   104,   108,   112,   119,   123,   130,   137,   141,
     145,   149,   153,   157,   164,   168,   172,   176,   193,   213,
     217,   224,   228,   235,   242,   246,   250,   254,   258,   265,
     269,   276,   280,   284,   288,   292,   299,   306,   307,   314,
     318,   325,   329,   347,   351,   355,   359,   363,   367,   374,
     378,   386,   390,   397,   402,   411,   412,   417,   421,   425,
     429,   433,   441,   449,   454,   455,   459,   463,   470,   474,
     493,   494,   498,   502,   509,   516,   520,   525,   531,   542,
     543,   548,   552,   559,   569,   584,   588,   592,   599,   608,
     609,   610,   614,   615,   618,   620
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "SHOW", "TABLES",
  "CREATE", "TABLE", "DROP", "DESC", "INSERT", "INTO", "VALUES", "DELETE",
  "FROM", "ASC", "ORDER", "BY", "WHERE", "UPDATE", "SET", "SELECT", "INT",
  "CHAR", "FLOAT", "BIGINT", "DATETIME", "INDEX", "AND", "JOIN", "ON",
  "EXIT", "HELP", "TXN_BEGIN", "TXN_COMMIT", "TXN_ABORT", "TXN_ROLLBACK",
  "ORDER_BY", "ENABLE_NESTLOOP", "ENABLE_SORTMERGE", "EXPLAIN", "MAX",
  "MIN", "COUNT", "SUM", "AS", "GROUP", "HAVING", "LIMIT", "SEMI",
  "STATIC_CHECKPOINT", "LEQ", "NEQ", "GEQ", "T_EOF", "IDENTIFIER",
  "VALUE_STRING", "VALUE_INT", "VALUE_BIGINT", "VALUE_FLOAT", "VALUE_BOOL",
  "';'", "'='", "'('", "')'", "','", "'.'", "'<'", "'>'", "'*'", "$accept",
  "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl", "dml",
  "fieldList", "colNameList", "field", "type", "valueList", "value",
  "condition", "optWhereClause", "whereClause", "col", "op", "expr",
  "selColList", "selCol", "optAlias", "aggFunc", "optGroupBy", "optHaving",
  "havingClause", "havingCondition", "optLimit", "setClauses", "setClause",
  "selector", "tableRef", "optOnClause", "fromList", "opt_order_clause",
  "order_clause", "opt_asc_desc", "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-147)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-105)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     127,    18,    12,    13,   -25,    22,    24,   -25,    16,    32,
    -147,  -147,  -147,  -147,  -147,  -147,    40,  -147,    42,     8,
    -147,  -147,  -147,  -147,  -147,  -147,    49,   -25,   -25,  -147,
     -25,   -25,  -147,  -147,   -25,   -25,    51,  -147,  -147,    15,
      25,    27,    30,    36,    29,  -147,    55,    43,  -147,    55,
      96,    46,  -147,    32,  -147,  -147,   -25,    50,    53,  -147,
      61,   102,   107,    71,    68,    77,    77,   -21,    77,    79,
    -147,    37,  -147,   -25,    71,   124,  -147,    71,    71,    71,
      76,    77,  -147,  -147,    -8,  -147,    81,  -147,    78,    85,
      92,    93,   100,  -147,  -147,  -147,    -7,    86,  -147,   -25,
      -5,  -147,    80,     2,  -147,    19,    95,  -147,   117,   -16,
      71,  -147,    95,  -147,  -147,  -147,  -147,  -147,   -25,   136,
     -25,   120,  -147,    -7,  -147,    71,  -147,   105,  -147,  -147,
    -147,  -147,  -147,    71,  -147,  -147,  -147,  -147,  -147,  -147,
      21,  -147,    77,  -147,  -147,  -147,  -147,  -147,  -147,    63,
    -147,  -147,   139,   -25,  -147,   153,   125,   120,  -147,   114,
    -147,  -147,    95,  -147,  -147,  -147,  -147,    77,  -147,   139,
      71,    37,   157,   125,   110,  -147,   117,  -147,   111,   -16,
     -16,   147,  -147,   160,   130,   157,  -147,    63,    95,    37,
      77,   122,  -147,   130,  -147,  -147,  -147,    41,   115,  -147,
    -147,  -147,  -147,  -147,    77,    41,  -147
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       4,     3,    11,    12,    13,    14,     0,     5,     0,     0,
       9,     6,    10,     7,     8,    15,     0,     0,     0,    23,
       0,     0,   104,    20,     0,     0,     0,   102,   103,     0,
       0,     0,     0,     0,   105,    85,    66,    86,    61,    66,
       0,     0,    52,     0,     1,     2,     0,     0,     0,    19,
       0,     0,    47,     0,     0,     0,     0,     0,     0,     0,
      64,     0,    63,     0,     0,     0,    16,     0,     0,     0,
       0,     0,    25,   105,    47,    82,     0,    17,     0,     0,
       0,     0,     0,    65,    62,    91,    47,    87,    51,     0,
       0,    29,     0,     0,    31,     0,     0,    49,    48,     0,
       0,    26,     0,    67,    68,    71,    70,    69,     0,     0,
       0,    73,    88,    47,    18,     0,    34,     0,    37,    35,
      38,    33,    21,     0,    22,    44,    41,    42,    43,    45,
       0,    39,     0,    57,    56,    58,    53,    54,    55,     0,
      83,    84,    89,     0,    92,     0,    75,    73,    30,     0,
      32,    24,     0,    50,    59,    60,    46,     0,    93,    89,
       0,     0,    96,    75,     0,    40,    90,    94,    72,     0,
       0,    74,    76,     0,    81,    96,    36,     0,     0,     0,
       0,     0,    27,    81,    78,    79,    77,   101,    95,    80,
      28,   100,    99,    97,     0,   101,    98
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -147,  -147,  -147,  -147,  -147,  -147,  -147,  -147,  -147,   -77,
      56,  -147,  -147,   -98,    44,   -56,    17,   -61,   -83,    -2,
    -147,   112,   133,  -146,    31,    14,  -147,     0,    -3,  -147,
      82,   138,  -107,    26,    94,     9,  -147,    -9,  -147,    -4,
     -62
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,   100,   103,
     101,   131,   140,   164,   107,    82,   108,    46,   149,   166,
      47,    48,    70,    49,   156,   172,   181,   182,   192,    84,
      85,    50,    95,   168,    96,   184,   198,   203,    39,    51,
      52
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      33,    86,   105,    36,    88,    89,    91,    92,   141,    81,
      81,   152,    98,   154,   151,   102,   104,   104,    27,    30,
     109,   118,    25,    57,    58,   180,    59,    60,   111,    32,
      61,    62,    34,    44,   143,   144,   145,    35,    28,    31,
     121,   119,    54,   180,    26,   146,   169,    90,    86,   201,
     147,   148,    76,    37,    38,   202,   110,   120,   124,   125,
      53,    29,    56,   102,   175,   132,   133,   157,    55,    97,
      63,   160,    40,    41,    42,    43,    64,    40,    41,    42,
      43,   109,   134,   133,   161,   162,    44,    65,   165,    66,
     195,    44,    67,   178,  -104,    97,   187,   188,    68,    69,
      45,   126,   127,   128,   129,   130,   109,    71,   104,    73,
     179,    74,    77,    80,    97,    78,    97,    44,   135,   136,
     137,   138,   139,    79,    81,    83,   165,    87,   179,   197,
       1,    44,     2,    93,     3,     4,     5,    99,   106,     6,
     122,   113,   112,   205,   142,     7,     8,     9,   114,    97,
     135,   136,   137,   138,   139,   115,   116,    10,    11,    12,
      13,    14,    15,   117,   153,   155,    16,   159,   167,   170,
     174,   171,   183,   186,   189,   133,   190,   191,   199,   204,
      17,   158,    72,    94,   176,   194,   163,   185,   173,   196,
     200,    75,   150,   123,   193,   177,   206
};

static const yytype_uint8 yycheck[] =
{
       4,    63,    79,     7,    65,    66,    67,    68,   106,    17,
      17,   118,    74,   120,   112,    77,    78,    79,     6,     6,
      81,    28,     4,    27,    28,   171,    30,    31,    84,    54,
      34,    35,    10,    54,    50,    51,    52,    13,    26,    26,
      96,    48,     0,   189,    26,    61,   153,    68,   110,     8,
      66,    67,    56,    37,    38,    14,    64,    64,    63,    64,
      20,    49,    13,   125,   162,    63,    64,   123,    60,    73,
      19,   133,    40,    41,    42,    43,    61,    40,    41,    42,
      43,   142,    63,    64,    63,    64,    54,    62,   149,    62,
     188,    54,    62,   170,    65,    99,   179,   180,    62,    44,
      68,    21,    22,    23,    24,    25,   167,    64,   170,    13,
     171,    65,    62,    11,   118,    62,   120,    54,    55,    56,
      57,    58,    59,    62,    17,    54,   187,    59,   189,   190,
       3,    54,     5,    54,     7,     8,     9,    13,    62,    12,
      54,    63,    61,   204,    27,    18,    19,    20,    63,   153,
      55,    56,    57,    58,    59,    63,    63,    30,    31,    32,
      33,    34,    35,    63,    28,    45,    39,    62,    29,    16,
      56,    46,    15,    63,    27,    64,    16,    47,    56,    64,
      53,   125,    49,    71,   167,   187,   142,   173,   157,   189,
     193,    53,   110,    99,   185,   169,   205
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      30,    31,    32,    33,    34,    35,    39,    53,    70,    71,
      72,    73,    74,    75,    76,     4,    26,     6,    26,    49,
       6,    26,    54,   108,    10,    13,   108,    37,    38,   107,
      40,    41,    42,    43,    54,    68,    86,    89,    90,    92,
     100,   108,   109,    20,     0,    60,    13,   108,   108,   108,
     108,   108,   108,    19,    61,    62,    62,    62,    62,    44,
      91,    64,    91,    13,    65,   100,   108,    62,    62,    62,
      11,    17,    84,    54,    98,    99,   109,    59,    86,    86,
      68,    86,    86,    54,    90,   101,   103,   108,   109,    13,
      77,    79,   109,    78,   109,    78,    62,    83,    85,    86,
      64,    84,    61,    63,    63,    63,    63,    63,    28,    48,
      64,    84,    54,   103,    63,    64,    21,    22,    23,    24,
      25,    80,    63,    64,    63,    55,    56,    57,    58,    59,
      81,    82,    27,    50,    51,    52,    61,    66,    67,    87,
      99,    82,   101,    28,   101,    45,    93,    84,    79,    62,
     109,    63,    64,    83,    82,    86,    88,    29,   102,   101,
      16,    46,    94,    93,    56,    82,    85,   102,    78,    86,
      92,    95,    96,    15,   104,    94,    63,    87,    87,    27,
      16,    47,    97,   104,    88,    82,    96,    86,   105,    56,
      97,     8,    14,   106,    64,    86,   106
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    69,    70,    70,    70,    70,    71,    71,    71,    71,
      71,    72,    72,    72,    72,    73,    73,    74,    75,    75,
      75,    75,    75,    75,    76,    76,    76,    76,    76,    77,
      77,    78,    78,    79,    80,    80,    80,    80,    80,    81,
      81,    82,    82,    82,    82,    82,    83,    84,    84,    85,
      85,    86,    86,    87,    87,    87,    87,    87,    87,    88,
      88,    89,    89,    90,    90,    91,    91,    92,    92,    92,
      92,    92,    93,    93,    94,    94,    95,    95,    96,    96,
      97,    97,    98,    98,    99,   100,   100,   101,   101,   102,
     102,   103,   103,   103,   103,   104,   104,   105,   105,   106,
     106,   106,   107,   107,   108,   109
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     2,     7,     4,     5,     9,    10,     1,
       3,     1,     3,     2,     1,     1,     4,     1,     1,     1,
       3,     1,     1,     1,     1,     1,     3,     0,     2,     1,
       3,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     2,     2,     2,     0,     4,     4,     4,
       4,     4,     3,     0,     2,     0,     1,     3,     3,     3,
       2,     0,     1,     3,     3,     1,     1,     1,     2,     0,
       2,     1,     3,     4,     5,     3,     0,     2,     4,     1,
       1,     0,     1,     1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* start: stmt ';'  */
#line 70 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1723 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 75 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1732 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 80 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1741 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 85 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1750 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 101 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1758 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 105 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1766 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 109 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1774 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 113 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1782 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 120 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1790 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 124 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndexes>((yyvsp[0].sv_str));
    }
#line 1798 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 131 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1806 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 138 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1814 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 142 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1822 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 146 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1830 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 150 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1838 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 154 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1846 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 23: /* ddl: CREATE STATIC_CHECKPOINT  */
#line 158 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CheckpointStmt>();
    }
#line 1854 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: INSERT INTO tbName VALUES '(' valueList ')'  */
#line 165 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-4].sv_str), (yyvsp[-1].sv_vals));
    }
#line 1862 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: DELETE FROM tbName optWhereClause  */
#line 169 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1870 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 173 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1878 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: SELECT selector FROM fromList optWhereClause optGroupBy optHaving opt_order_clause optLimit  */
#line 177 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto all_conds = (yyvsp[-4].sv_conds);
        for (auto& jc : (yyvsp[-5].sv_from_clause)->join_conds) {
            all_conds.push_back(jc);
        }
        auto stmt = std::make_shared<SelectStmt>((yyvsp[-7].sv_sel_cols), (yyvsp[-5].sv_from_clause)->tab_names, all_conds, (yyvsp[-1].sv_orderby));
        stmt->aliases = std::move((yyvsp[-5].sv_from_clause)->aliases);
        stmt->group_by = (yyvsp[-3].sv_cols);
        stmt->having_conds = (yyvsp[-2].sv_conds);
        stmt->limit_count = (yyvsp[0].sv_int);
        stmt->is_semi_join = (yyvsp[-5].sv_from_clause)->is_semi_join;
        if ((yyvsp[-5].sv_from_clause)->is_semi_join && !(yyvsp[-5].sv_from_clause)->tab_names.empty()) {
            stmt->semi_left_table = (yyvsp[-5].sv_from_clause)->tab_names[0];
        }
        (yyval.sv_node) = std::move(stmt);
    }
#line 1899 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: EXPLAIN SELECT selector FROM fromList optWhereClause optGroupBy optHaving opt_order_clause optLimit  */
#line 194 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto all_conds = (yyvsp[-4].sv_conds);
        for (auto& jc : (yyvsp[-5].sv_from_clause)->join_conds) {
            all_conds.push_back(jc);
        }
        auto select_stmt = std::make_shared<SelectStmt>((yyvsp[-7].sv_sel_cols), (yyvsp[-5].sv_from_clause)->tab_names, all_conds, (yyvsp[-1].sv_orderby));
        select_stmt->aliases = std::move((yyvsp[-5].sv_from_clause)->aliases);
        select_stmt->group_by = (yyvsp[-3].sv_cols);
        select_stmt->having_conds = (yyvsp[-2].sv_conds);
        select_stmt->limit_count = (yyvsp[0].sv_int);
        select_stmt->is_semi_join = (yyvsp[-5].sv_from_clause)->is_semi_join;
        if ((yyvsp[-5].sv_from_clause)->is_semi_join && !(yyvsp[-5].sv_from_clause)->tab_names.empty()) {
            select_stmt->semi_left_table = (yyvsp[-5].sv_from_clause)->tab_names[0];
        }
        (yyval.sv_node) = std::make_shared<ExplainStmt>(std::move(select_stmt));
    }
#line 1920 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 29: /* fieldList: field  */
#line 214 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 1928 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 30: /* fieldList: fieldList ',' field  */
#line 218 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 1936 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 31: /* colNameList: colName  */
#line 225 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 1944 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 32: /* colNameList: colNameList ',' colName  */
#line 229 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 1952 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 33: /* field: colName type  */
#line 236 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 1960 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 34: /* type: INT  */
#line 243 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 1968 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 35: /* type: BIGINT  */
#line 247 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_BIGINT, sizeof(int64_t));
    }
#line 1976 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 36: /* type: CHAR '(' VALUE_INT ')'  */
#line 251 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 1984 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 37: /* type: FLOAT  */
#line 255 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 1992 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 38: /* type: DATETIME  */
#line 259 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_DATETIME, sizeof(int64_t));
    }
#line 2000 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 39: /* valueList: value  */
#line 266 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2008 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 40: /* valueList: valueList ',' value  */
#line 270 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2016 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 41: /* value: VALUE_INT  */
#line 277 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2024 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 42: /* value: VALUE_BIGINT  */
#line 281 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BigIntLit>((yyvsp[0].sv_bigint));
    }
#line 2032 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 43: /* value: VALUE_FLOAT  */
#line 285 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2040 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 44: /* value: VALUE_STRING  */
#line 289 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2048 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 45: /* value: VALUE_BOOL  */
#line 293 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2056 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 46: /* condition: col op expr  */
#line 300 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2064 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 47: /* optWhereClause: %empty  */
#line 306 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_conds) = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
#line 2070 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 48: /* optWhereClause: WHERE whereClause  */
#line 308 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2078 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 49: /* whereClause: condition  */
#line 315 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2086 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 50: /* whereClause: whereClause AND condition  */
#line 319 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2094 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 51: /* col: tbName '.' colName  */
#line 326 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2102 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 52: /* col: colName  */
#line 330 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2110 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 53: /* op: '='  */
#line 348 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2118 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 54: /* op: '<'  */
#line 352 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2126 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 55: /* op: '>'  */
#line 356 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2134 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 56: /* op: NEQ  */
#line 360 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2142 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 57: /* op: LEQ  */
#line 364 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2150 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 58: /* op: GEQ  */
#line 368 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2158 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 59: /* expr: value  */
#line 375 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2166 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 60: /* expr: col  */
#line 379 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2174 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 61: /* selColList: selCol  */
#line 387 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_cols) = std::vector<std::shared_ptr<SelectCol>>{(yyvsp[0].sv_sel_col)};
    }
#line 2182 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 62: /* selColList: selColList ',' selCol  */
#line 391 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_cols).push_back((yyvsp[0].sv_sel_col));
    }
#line 2190 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 63: /* selCol: aggFunc optAlias  */
#line 398 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = (yyvsp[-1].sv_sel_col);
        if (!(yyvsp[0].sv_str).empty()) (yyval.sv_sel_col)->alias = (yyvsp[0].sv_str);
    }
#line 2199 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 64: /* selCol: col optAlias  */
#line 403 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto sc = std::make_shared<SelectCol>((yyvsp[-1].sv_col));
        if (!(yyvsp[0].sv_str).empty()) sc->alias = (yyvsp[0].sv_str);
        (yyval.sv_sel_col) = sc;
    }
#line 2209 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 65: /* optAlias: AS IDENTIFIER  */
#line 411 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_str) = (yyvsp[0].sv_str); }
#line 2215 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 66: /* optAlias: %empty  */
#line 412 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_str) = ""; }
#line 2221 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 67: /* aggFunc: MAX '(' col ')'  */
#line 418 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = std::make_shared<SelectCol>(AGG_MAX, (yyvsp[-1].sv_col));
    }
#line 2229 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 68: /* aggFunc: MIN '(' col ')'  */
#line 422 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = std::make_shared<SelectCol>(AGG_MIN, (yyvsp[-1].sv_col));
    }
#line 2237 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 69: /* aggFunc: SUM '(' col ')'  */
#line 426 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = std::make_shared<SelectCol>(AGG_SUM, (yyvsp[-1].sv_col));
    }
#line 2245 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 70: /* aggFunc: COUNT '(' col ')'  */
#line 430 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = std::make_shared<SelectCol>(AGG_COUNT, (yyvsp[-1].sv_col));
    }
#line 2253 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 71: /* aggFunc: COUNT '(' '*' ')'  */
#line 434 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_col) = std::make_shared<SelectCol>(AGG_COUNT_STAR, nullptr);
    }
#line 2261 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 72: /* optGroupBy: GROUP BY colNameList  */
#line 442 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        std::vector<std::shared_ptr<Col>> cols;
        for (auto& name : (yyvsp[0].sv_strs)) {
            cols.push_back(std::make_shared<Col>("", name));
        }
        (yyval.sv_cols) = cols;
    }
#line 2273 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 73: /* optGroupBy: %empty  */
#line 449 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>(); }
#line 2279 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 74: /* optHaving: HAVING havingClause  */
#line 454 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                            { (yyval.sv_conds) = (yyvsp[0].sv_conds); }
#line 2285 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 75: /* optHaving: %empty  */
#line 455 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_conds) = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
#line 2291 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 76: /* havingClause: havingCondition  */
#line 460 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<ast::BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2299 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 77: /* havingClause: havingClause AND havingCondition  */
#line 464 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2307 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 78: /* havingCondition: col op expr  */
#line 471 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<ast::BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2315 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 79: /* havingCondition: aggFunc op value  */
#line 475 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        // 将聚合函数转换为伪列：col_name = 聚合函数表达式
        std::string pseudo_col;
        switch ((yyvsp[-2].sv_sel_col)->agg_type) {
            case ast::AGG_MAX:  pseudo_col = "MAX(" + ((yyvsp[-2].sv_sel_col)->col ? (yyvsp[-2].sv_sel_col)->col->col_name : "") + ")"; break;
            case ast::AGG_MIN:  pseudo_col = "MIN(" + ((yyvsp[-2].sv_sel_col)->col ? (yyvsp[-2].sv_sel_col)->col->col_name : "") + ")"; break;
            case ast::AGG_COUNT: pseudo_col = "COUNT(" + ((yyvsp[-2].sv_sel_col)->col ? (yyvsp[-2].sv_sel_col)->col->col_name : "") + ")"; break;
            case ast::AGG_SUM:  pseudo_col = "SUM(" + ((yyvsp[-2].sv_sel_col)->col ? (yyvsp[-2].sv_sel_col)->col->col_name : "") + ")"; break;
            case ast::AGG_COUNT_STAR: pseudo_col = "COUNT(*)"; break;
            default: pseudo_col = "agg"; break;
        }
        auto col_ref = std::make_shared<ast::Col>("", pseudo_col);
        (yyval.sv_cond) = std::make_shared<ast::BinaryExpr>(col_ref, (yyvsp[-1].sv_comp_op), std::static_pointer_cast<ast::Expr>((yyvsp[0].sv_val)));
    }
#line 2334 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 80: /* optLimit: LIMIT VALUE_INT  */
#line 493 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                        { (yyval.sv_int) = (yyvsp[0].sv_int); }
#line 2340 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 81: /* optLimit: %empty  */
#line 494 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_int) = -1; }
#line 2346 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 82: /* setClauses: setClause  */
#line 499 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2354 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 83: /* setClauses: setClauses ',' setClause  */
#line 503 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2362 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 84: /* setClause: colName '=' value  */
#line 510 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2370 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 85: /* selector: '*'  */
#line 517 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_sel_cols) = {};  // 空列表表示 SELECT *
    }
#line 2378 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 87: /* tableRef: tbName  */
#line 526 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto fc = std::make_shared<ast::FromClause>();
        fc->tab_names.push_back((yyvsp[0].sv_str));
        (yyval.sv_from_clause) = fc;
    }
#line 2388 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 88: /* tableRef: tbName IDENTIFIER  */
#line 532 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto fc = std::make_shared<ast::FromClause>();
        fc->tab_names.push_back((yyvsp[-1].sv_str));
        fc->aliases[(yyvsp[0].sv_str)] = (yyvsp[-1].sv_str);   // 别名 → 真实表名
        (yyval.sv_from_clause) = fc;
    }
#line 2399 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 89: /* optOnClause: %empty  */
#line 542 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_conds) = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
#line 2405 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 90: /* optOnClause: ON whereClause  */
#line 543 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                       { (yyval.sv_conds) = (yyvsp[0].sv_conds); }
#line 2411 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 91: /* fromList: tableRef  */
#line 549 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_from_clause) = (yyvsp[0].sv_from_clause);
    }
#line 2419 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 92: /* fromList: fromList ',' tableRef  */
#line 553 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto& dst = (yyvsp[-2].sv_from_clause)->tab_names;
        dst.insert(dst.end(), (yyvsp[0].sv_from_clause)->tab_names.begin(), (yyvsp[0].sv_from_clause)->tab_names.end());
        (yyvsp[-2].sv_from_clause)->aliases.insert((yyvsp[0].sv_from_clause)->aliases.begin(), (yyvsp[0].sv_from_clause)->aliases.end());
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2430 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 93: /* fromList: fromList JOIN tableRef optOnClause  */
#line 560 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto& dst = (yyvsp[-3].sv_from_clause)->tab_names;
        dst.insert(dst.end(), (yyvsp[-1].sv_from_clause)->tab_names.begin(), (yyvsp[-1].sv_from_clause)->tab_names.end());
        (yyvsp[-3].sv_from_clause)->aliases.insert((yyvsp[-1].sv_from_clause)->aliases.begin(), (yyvsp[-1].sv_from_clause)->aliases.end());
        for (auto& cond : (yyvsp[0].sv_conds)) {
            (yyvsp[-3].sv_from_clause)->join_conds.push_back(cond);
        }
        (yyval.sv_from_clause) = (yyvsp[-3].sv_from_clause);
    }
#line 2444 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 94: /* fromList: fromList SEMI JOIN tableRef optOnClause  */
#line 570 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        auto& dst = (yyvsp[-4].sv_from_clause)->tab_names;
        dst.insert(dst.end(), (yyvsp[-1].sv_from_clause)->tab_names.begin(), (yyvsp[-1].sv_from_clause)->tab_names.end());
        (yyvsp[-4].sv_from_clause)->aliases.insert((yyvsp[-1].sv_from_clause)->aliases.begin(), (yyvsp[-1].sv_from_clause)->aliases.end());
        for (auto& cond : (yyvsp[0].sv_conds)) {
            (yyvsp[-4].sv_from_clause)->join_conds.push_back(cond);
        }
        (yyvsp[-4].sv_from_clause)->is_semi_join = true;
        (yyval.sv_from_clause) = (yyvsp[-4].sv_from_clause);
    }
#line 2459 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 95: /* opt_order_clause: ORDER BY order_clause  */
#line 585 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = (yyvsp[0].sv_orderby);
    }
#line 2467 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 96: /* opt_order_clause: %empty  */
#line 588 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                      { (yyval.sv_orderby) = nullptr; }
#line 2473 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 97: /* order_clause: col opt_asc_desc  */
#line 593 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyval.sv_orderby) = std::make_shared<OrderBy>(
            std::vector<std::shared_ptr<Col>>{(yyvsp[-1].sv_col)},
            std::vector<OrderByDir>{(yyvsp[0].sv_orderby_dir)}
        );
    }
#line 2484 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 98: /* order_clause: order_clause ',' col opt_asc_desc  */
#line 600 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
    {
        (yyvsp[-3].sv_orderby)->cols.push_back((yyvsp[-1].sv_col));
        (yyvsp[-3].sv_orderby)->orderby_dirs.push_back((yyvsp[0].sv_orderby_dir));
        (yyval.sv_orderby) = (yyvsp[-3].sv_orderby);
    }
#line 2494 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 99: /* opt_asc_desc: ASC  */
#line 608 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2500 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 100: /* opt_asc_desc: DESC  */
#line 609 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2506 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 101: /* opt_asc_desc: %empty  */
#line 610 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2512 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 102: /* set_knob_type: ENABLE_NESTLOOP  */
#line 614 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2518 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;

  case 103: /* set_knob_type: ENABLE_SORTMERGE  */
#line 615 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2524 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"
    break;


#line 2528 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.tab.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 621 "/mnt/d/Python_Project/RMDB_proj/rmdb/src/parser/yacc.y"

