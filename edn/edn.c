#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "lib/stringinfo.h"
#include "common/hashfn.h"
#include "string.h"
#include "stdbool.h"
#include "stdarg.h"

PG_MODULE_MAGIC;

/* Guard against stack exhaustion from pathologically nested input. */
#define EDN_MAX_DEPTH 1000

typedef enum
{
  EDN_INVALID,
  EDN_STRING,
  EDN_MAP_START,
  EDN_MAP_END,
  EDN_UNKNOWN,
  EDN_EOF
} EDNTokenType;

typedef struct EDNValue
{
  enum
  {
    EDN_STRING_TYPE,
    EDN_MAP_TYPE
  } type;

  int size;
  bool hashed;

  union
  {
    struct
    {
      char *value;
    } string;

    struct
    {
      uint32_t hash;
      struct EDNMapEntry *entries;
    } map;
  } data;

} EDNValue;

typedef struct EDNMapEntry
{
  struct EDNValue key;
  struct EDNValue value;
} EDNMapEntry;

typedef struct EDNLexicalContext
{
  char *input;
  char *current_token;
  EDNTokenType current_token_type;
  int nest_level;
} EDNLexicalContext;

static bool advance_parser(EDNLexicalContext *lexical_context, int vararg_count, ...);
static bool is_whitespace(char token);
static void parse_token(EDNLexicalContext *lexical_context);
static EDNValue *parse_edn(EDNLexicalContext *lexical_context);
static EDNValue *parse_map(EDNLexicalContext *lexical_context);
static EDNValue *parse_string(EDNLexicalContext *lexical_context);
static uint32_t hash_edn_value(EDNValue *value, bool refresh_cached_hash_values);
static bool has_unique_values(EDNValue *values, int len);
static bool compare_vals(EDNValue a, EDNValue b);
static EDNValue *get_map_keys(EDNValue *map);
static void serialize_edn_value(EDNValue *value, StringInfo out);

PG_FUNCTION_INFO_V1(edn_in);
Datum edn_in(PG_FUNCTION_ARGS)
{
  char *in = PG_GETARG_CSTRING(0);
  EDNLexicalContext *lex;
  EDNValue *result;
  StringInfoData buf;

  lex = palloc0(sizeof(EDNLexicalContext));
  lex->input = in;
  lex->current_token = in;
  lex->nest_level = -1;

  advance_parser(lex, 2, EDN_MAP_START, EDN_STRING);

  result = parse_edn(lex);

  /* Reject anything trailing the top-level value (e.g. "{} garbage"). */
  parse_token(lex);
  if (lex->current_token_type != EDN_EOF)
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
             errmsg("trailing characters after EDN value")));

  initStringInfo(&buf);
  serialize_edn_value(result, &buf);

  PG_RETURN_TEXT_P(cstring_to_text_with_len(buf.data, buf.len));
}

PG_FUNCTION_INFO_V1(edn_out);
Datum edn_out(PG_FUNCTION_ARGS)
{
  text *stored = PG_GETARG_TEXT_PP(0);

  PG_RETURN_CSTRING(text_to_cstring(stored));
}

static bool advance_parser(EDNLexicalContext *lexical_context, int vararg_count, ...)
{
  va_list varargs;

  parse_token(lexical_context);

  va_start(varargs, vararg_count);

  for (int i = 0; i < vararg_count; i++)
  {
    EDNTokenType token_type = va_arg(varargs, EDNTokenType);
    if (lexical_context->current_token_type == token_type)
    {
      va_end(varargs);
      return true;
    }
  }

  va_end(varargs);

  return false;
}

static void parse_token(EDNLexicalContext *lexical_context)
{
  char *t = lexical_context->current_token;

  while (is_whitespace(*t))
  {
    t++;
  }

  switch (*t)
  {
  case '\0':
    lexical_context->current_token = t;
    lexical_context->current_token_type = EDN_EOF;
    break;
  case '{':
    lexical_context->current_token = t + 1;
    lexical_context->current_token_type = EDN_MAP_START;
    break;
  case '}':
    lexical_context->current_token = t + 1;
    lexical_context->current_token_type = EDN_MAP_END;
    break;
  case '"':
    lexical_context->current_token = t;
    lexical_context->current_token_type = EDN_STRING;
    break;
  default:
    lexical_context->current_token = t;
    lexical_context->current_token_type = EDN_UNKNOWN;
    break;
  }
}

static bool is_whitespace(char token)
{
  if (token == ' ' || token == '\t' || token == '\n' || token == '\r' || token == ',')
  {
    return true;
  }

  return false;
}

static EDNValue *parse_edn(EDNLexicalContext *lexical_context)
{
  switch (lexical_context->current_token_type)
  {
  case EDN_MAP_START:
    return parse_map(lexical_context);
  case EDN_STRING:
    return parse_string(lexical_context);
  case EDN_EOF:
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
             errmsg("unexpected end of input")));
    break;
  default:
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
             errmsg("unsupported EDN value (only maps and strings are currently supported)")));
    break;
  }

  return NULL;
}

static EDNValue *parse_map(EDNLexicalContext *lexical_context)
{
  EDNValue *map = palloc0(sizeof(EDNValue));
  int size = 0;

  map->type = EDN_MAP_TYPE;

  lexical_context->nest_level += 1;

  if (lexical_context->nest_level > EDN_MAX_DEPTH)
    ereport(ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg("EDN nesting too deep (limit %d)", EDN_MAX_DEPTH)));

  while (advance_parser(lexical_context, 2, EDN_STRING, EDN_MAP_START))
  {
    EDNValue *key;
    EDNValue *value;

    if (size == 0)
    {
      map->data.map.entries = palloc0(sizeof(EDNMapEntry));
    }
    else
    {
      map->data.map.entries = repalloc(map->data.map.entries, sizeof(EDNMapEntry) * (size + 1));
    }

    key = parse_edn(lexical_context);

    if (!advance_parser(lexical_context, 2, EDN_STRING, EDN_MAP_START))
    {
      switch (lexical_context->current_token_type)
      {
      case EDN_MAP_END:
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Map literal must contain an even number of forms")));
        break;
      default:
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Unexpected token when parsing value for map entry")));
        break;
      }
    }

    value = parse_edn(lexical_context);

    map->data.map.entries[size].key = *key;
    map->data.map.entries[size].value = *value;

    size++;
  }

  /* we've finished parsing map, ensure the last token we saw was a map end */
  if (lexical_context->current_token_type != EDN_MAP_END)
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Expected map terminator")));

  map->size = size;

  hash_edn_value(map, false);

  if (!has_unique_values(get_map_keys(map), size))
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Duplicate key in map")));

  lexical_context->nest_level -= 1;

  return map;
}

static EDNValue *get_map_keys(EDNValue *map)
{
  EDNValue *keys = palloc0(sizeof(EDNValue) * map->size);

  for (int i = 0; i < map->size; i++)
  {
    keys[i] = map->data.map.entries[i].key;
  }

  return keys;
}

static uint32_t hash_edn_value(EDNValue *value, bool refresh_cached_hash_values)
{
  uint32_t hash = 0;

  switch (value->type)
  {
  case EDN_MAP_TYPE:
    if (!value->hashed || refresh_cached_hash_values)
    {
      for (int i = 0; i < value->size; i++)
      {
        hash += hash_edn_value(&value->data.map.entries[i].key, refresh_cached_hash_values) ^ hash_edn_value(&value->data.map.entries[i].value, refresh_cached_hash_values);
      }

      value->data.map.hash = hash;
      value->hashed = true;
    }
    else
    {
      hash = value->data.map.hash;
    }
    break;
  default:
    hash = hash_bytes((const unsigned char *) value->data.string.value,
                      strlen(value->data.string.value));
    break;
  }

  return hash;
}

static bool has_unique_values(EDNValue *values, int len)
{
  bool unique_vals = true;

  for (int i = 0; i < len; i++)
  {
    for (int j = i + 1; j < len; j++)
    {
      if (compare_vals(values[i], values[j]))
      {
        unique_vals = false;
        break;
      }
    }

    if (!unique_vals)
    {
      break;
    }
  }

  return unique_vals;
}

static bool compare_vals(EDNValue a, EDNValue b)
{
  bool vals_equal = true;

  if ((a.type != b.type) || (a.size != b.size))
  {
    vals_equal = false;
  }
  else
  {
    switch (a.type)
    {
    case EDN_STRING_TYPE:
      if (strcmp(a.data.string.value, b.data.string.value) != 0)
      {
        vals_equal = false;
      }
      break;
    case EDN_MAP_TYPE:
      if (a.data.map.hash != b.data.map.hash)
      {
        vals_equal = false;
      }
      break;
    default:
      break;
    }
  }

  return vals_equal;
}

static EDNValue *parse_string(EDNLexicalContext *lexical_context)
{
  char *t, *string_start;
  int len = 0;

  EDNValue *value = palloc0(sizeof(EDNValue));

  t = string_start = lexical_context->current_token;

  for (;;)
  {
    t++;

    if (*t == '\0')
    {
      ereport(ERROR,
              (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
               errmsg("unterminated string literal")));
    }

    /* we've reached the end of the string */
    if (*t == '"')
    {
      break;
    }

    len++;
  }

  value->data.string.value = palloc0(sizeof(char) * (len + 1));

  /* skip opening double quote */
  string_start++;

  strncpy(value->data.string.value, string_start, len);
  value->data.string.value[len] = '\0';

  lexical_context->current_token = t + 1;

  value->size = len + 1;
  value->type = EDN_STRING_TYPE;

  return value;
}

static void serialize_edn_value(EDNValue *value, StringInfo out)
{
  switch (value->type)
  {
  case EDN_MAP_TYPE:
    appendStringInfoChar(out, '{');
    for (int i = 0; i < value->size; i++)
    {
      if (i > 0)
        appendStringInfoString(out, ", ");

      serialize_edn_value(&value->data.map.entries[i].key, out);
      appendStringInfoChar(out, ' ');
      serialize_edn_value(&value->data.map.entries[i].value, out);
    }
    appendStringInfoChar(out, '}');
    break;
  case EDN_STRING_TYPE:
    appendStringInfoChar(out, '"');
    appendStringInfoString(out, value->data.string.value);
    appendStringInfoChar(out, '"');
    break;
  }
}
