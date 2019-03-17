#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"
#include "string.h"
#include "stdbool.h"
#include "stdarg.h"
#include "access/hash.h"
#include "utils/array.h"
#include "utils/datum.h"
#include "catalog/pg_type.h"

#include "murmur3.h"

PG_MODULE_MAGIC;

typedef enum
{
  EDN_INVALID,
  EDN_STRING,
  EDN_NUMBER,
  EDN_SYMBOL,
  EDN_KEYWORD,
  EDN_BOOLEAN,
  EDN_MAP_START,
  EDN_MAP_END,
  EDN_END,
  EDN_UNKNOWN
} EDNTokenType;

typedef enum
{
  EDN_GET_VALUE,
  EDN_SET_VALUE,
  EDN_DELETE_VALUE
} EDNActionType;

typedef struct EDNAction
{
  EDNActionType type;
  EDNValue *action_path;
  EDNValue *data;
} EDNAction;

typedef struct EDNValue
{
  enum { EDN_STRING_TYPE, EDN_INTEGER_TYPE, EDN_MAP_TYPE, EDN_HALT_PARSER_TYPE } type;

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

    struct
    {
      struct EDNValue *items;
    } vector;

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
  int input_length;
  char *current_token;
  EDNTokenType current_token_type;
  int nest_level;
  EDNValue *current_path;
  EDNAction *action;
} EDNLexicalContext;

bool advance_parser(EDNLexicalContext *lexical_context, int vararg_count, ...);
bool is_whitespace(char token);
void parse_token(EDNLexicalContext *lexical_context);
EDNValue * parse_edn(EDNLexicalContext *lexical_context);
EDNValue * parse_map(EDNLexicalContext *lexical_context);
EDNValue * parse_string(EDNLexicalContext *lexical_context);
uint32_t hash_edn_value(EDNValue *value, bool refresh_cached_hash_values);
bool has_unique_values(EDNValue *values, int len);
bool compare_vals (EDNValue a, EDNValue b);
EDNValue * get_map_keys (EDNValue * map);

PG_FUNCTION_INFO_V1(edn_in);
Datum edn_in(PG_FUNCTION_ARGS)
{
  char *in = PG_GETARG_CSTRING(0);

  EDNLexicalContext *lex = palloc0(sizeof(EDNLexicalContext));
  lex->input = in;
  lex->current_token = in;
  lex->current_path = palloc0(sizeof(EDNValue) * 10);
  lex->nest_level = -1;

  advance_parser(lex, 1, EDN_MAP_START);

  EDNValue *result = parse_edn(lex);

  PG_RETURN_TEXT_P(cstring_to_text("hello world"));
}

PG_FUNCTION_INFO_V1(edn_out);
Datum edn_out(PG_FUNCTION_ARGS)
{
  PG_RETURN_CSTRING("Hello World");
}

PG_FUNCTION_INFO_V1(deconstruct_array_input);
Datum deconstruct_array_input(PG_FUNCTION_ARGS)
{
  ArrayType *array;
  Datum *path_datums;
  bool *path_nulls;
  int path_count;

  array = PG_GETARG_ARRAYTYPE_P(0);

  deconstruct_array(array, TEXTOID, -1, false, 'i', &path_datums, &path_nulls, &path_count);

  char *path_components = palloc0(sizeof(char) * path_count);

  for(int i = 0; i < path_count; i++)
  {
    char *path_component = TextDatumGetCString(path_datums[i]);

    EDNLexicalContext *lex = palloc0(sizeof(EDNLexicalContext));
    lex->input = path_component;
    lex->current_token = path_component;
    lex->current_path = palloc0(sizeof(EDNValue) * 10);
    lex->nest_level = -1;

    advance_parser(lex, 2, EDN_MAP_START, EDN_STRING);
    EDNValue *result = parse_edn(lex);
  }

  PG_RETURN_CSTRING("Hello World");
}

bool advance_parser(EDNLexicalContext *lexical_context, int vararg_count, ...)
{
  va_list varargs;

  parse_token(lexical_context);

  va_start(varargs, vararg_count);

  for(int i = 0; i < vararg_count; i ++)
  {
    EDNTokenType token_type = va_arg(varargs, EDNTokenType);
    if(lexical_context->current_token_type == token_type)
    {
      va_end(varargs);
      return true;
    }
  }

  va_end(varargs);

  return false;
}

void parse_token(EDNLexicalContext *lexical_context)
{
    char *t = lexical_context->current_token;
    char *lookahead;

    //Need to add length checks here, we can't be setting t to something that is outside the length of our input!
    while(is_whitespace(*t))
    {
      t ++;
    }

    lookahead = t + 1;

    switch(*t)
    {
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
        lexical_context->current_token_type = EDN_UNKNOWN;
        break;
    }
}

bool is_whitespace(char token)
{
  if(token == ' ' || token == '\n' || token == '\r' || token == ',')
  {
    return true;
  }

  return false;
}

EDNValue * parse_edn(EDNLexicalContext *lexical_context)
{
  EDNValue *value = palloc0(sizeof(EDNValue));

  switch(lexical_context->current_token_type)
  {
    case EDN_MAP_START:
      value = parse_map(lexical_context);
      break;
    case EDN_STRING:
      value = parse_string(lexical_context);
    default:
      break;
  }

  return value;
}

EDNValue * parse_map(EDNLexicalContext *lexical_context)
{
  EDNValue *map = palloc0(sizeof(EDNValue));
  map->type = EDN_MAP_TYPE;

  int size = 0;

  lexical_context->nest_level += 1;

  while(advance_parser(lexical_context, 2, EDN_STRING, EDN_MAP_START))
  {
    EDNMapEntry *entry = palloc0(sizeof(EDNMapEntry));

    if(size == 0)
    {
      map->data.map.entries = palloc0(sizeof(EDNMapEntry));
    }
    else
    {
      map->data.map.entries = repalloc(map->data.map.entries, sizeof(EDNMapEntry) * (size + 1));
    }

    EDNValue * key = parse_edn(lexical_context);

    if(key->type == EDN_HALT_PARSER_TYPE)
    {
      return key;
    }

    entry->key = *key;

    lexical_context->current_path[lexical_context->nest_level] = *key;

    if(!advance_parser(lexical_context, 2, EDN_STRING, EDN_MAP_START))
    {
      switch(lexical_context->current_token_type)
      {
        case EDN_MAP_END:
          ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Map literal must contain an even number of forms")));
          break;
        default:
          ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Unexpected token when parsing value for map entry")));
          break;
      }
    }

    EDNValue * value = parse_edn(lexical_context);

    if(key->type == EDN_HALT_PARSER_TYPE)
    {
      return key;
    }

    entry->value = *value;

    map->data.map.entries[size] = *entry;

    size++;

    for(int i = 0; i < lexical_context->nest_level + 1; i ++)
    {
      printf("%s%s", " ", lexical_context->current_path[i].data.string.value);
    }

    printf("\n");
  }

  // we've finished parsing map, ensure the last token we saw was a map end
  if(!(lexical_context->current_token_type == EDN_MAP_END))
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Expected map terminator")));

  map->size = size;

  hash_edn_value(map, false);

  if(!has_unique_values(get_map_keys(map), size))
    ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Duplicate key in map")));

  lexical_context->nest_level -= 1;

  return map;
}

EDNValue * get_map_keys (EDNValue * map)
{
  EDNValue * keys = palloc0(sizeof(EDNValue) * map->size);

  for(int i = 0; i < map->size; i++)
  {
    keys[i] = map->data.map.entries[i].key;
  }

  return keys;
}

uint32_t hash_edn_value(EDNValue *value, bool refresh_cached_hash_values)
{
  uint32_t hash = 0;

  switch(value->type)
  {
    case EDN_MAP_TYPE:
      if(!value->hashed || refresh_cached_hash_values)
      {
        for(int i = 0; i < value->size; i ++)
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
      MurmurHash3_x86_32(value->data.string.value, strlen(value->data.string.value), 0, &hash);
      break;
  }

  return hash;
}

bool has_unique_values(EDNValue *values, int len)
{
  bool unique_vals = true;

  for(int i = 0; i < len; i ++)
  {
    for(int j = i + 1; j < len; j ++)
    {
      if(compare_vals(values[i], values[j]))
      {
        unique_vals = false;
        break;
      }
    }

    if(!unique_vals)
    {
      break;
    }
  }

  return unique_vals;
}

bool compare_vals(EDNValue a, EDNValue b)
{
  bool vals_equal = true;

  if((a.type != b.type) || (a.size != b.size))
  {
    vals_equal = false;
  }
  else
  {
    switch(a.type)
    {
      case EDN_STRING_TYPE:
        if(strcmp(a.data.string.value, b.data.string.value) != 0)
        {
          vals_equal = false;
        }
        break;
      case EDN_MAP_TYPE:
        if(a.data.map.hash != b.data.map.hash)
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

EDNValue * parse_string(EDNLexicalContext *lexical_context)
{
  char *t, *string_start;
  int len = 0;

  EDNValue *value = palloc0(sizeof(EDNValue));

  t = string_start = lexical_context->current_token;

  for(;;)
  {
    t++;

    // we've reached the end of the string
    if (*t == '"')
    {
      break;
    }

    len++;
  }

  value->data.string.value = palloc0(sizeof(char) * (len + 1));

  // skip first double quotes
  string_start++;

  memset(value->data.string.value, '\0', sizeof(char) * (len + 1));
  strncpy(value->data.string.value, string_start, len);

  lexical_context->current_token = t + 1;

  value->size = len + 1;
  value->type = EDN_STRING_TYPE;

  return value;
}
