#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdarg.h"
#include "windows.h"
#include "stdint.h"
#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "lib/stringinfo.h"

PG_MODULE_MAGIC;

#define SIZE_MASK 0x0FFFFFFF
#define TYPE_MASK 0xF0000000
#define EDNB_MAP 0x10000000
#define EDNB_VECTOR 0x20000000
#define EDNB_STRING 0x30000000

typedef struct EDNValue
{
  enum
  {
    EDN_STRING,
    EDN_INTEGER,
    EDN_MAP
  } type;

  int size;

  union Value
  {
    char *string;
    struct EDNValuePair *entries;
  };

} EDNValue;

typedef struct EDNValuePair
{
  struct EDNValue key;
  struct EDNValue value;
} EDNValuePair;

typedef struct EDNB
{
  uint32_t vl_len_;
  char data[];
} EDNB;

void EDNBValue_to_EDNB(EDNValue *val, StringInfo buffer);

PG_FUNCTION_INFO_V1(ednb_in);
Datum ednb_in(PG_FUNCTION_ARGS)
{
  char *input_str = PG_GETARG_CSTRING(0);

  EDNValue *val = parse_edn_string(input_str);

  StringInfo buffer = makeStringInfo();
  EDNBValue_to_EDNB(val, buffer);

  EDNB *result = (EDNB *)palloc(buffer->len + VARHDRSZ);
  SET_VARSIZE(result, buffer->len + VARHDRSZ);
  memcpy(result->data, buffer->data, buffer->len);

  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ednb_out);
Datum ednb_out(PG_FUNCTION_ARGS)
{
  PG_RETURN_CSTRING("");
}

void EDNBValue_to_EDNB(EDNValue *val, StringInfo buffer)
{
  uint32_t header, size;

  size = val->size;

  switch (val->type)
  {
  case EDN_MAP:
    header = EDNB_MAP | size;
    appendBinaryStringInfoNT(buffer, (char *)&header, sizeof(uint32_t));

    for (int i = 0; i < size; i++)
    {
      EDNBValue_to_EDNB(&val->entries[i].key, buffer);
      EDNBValue_to_EDNB(&val->entries[i].value, buffer);
    }
    break;
  case EDN_STRING:
    header = EDNB_STRING | size;
    appendBinaryStringInfoNT(buffer, (char *)&header, sizeof(uint32_t));
    appendBinaryStringInfoNT(buffer, val->string, size);
    break;
  default:
    break;
  }
}

// EDNValue * EDNB_to_EDNValue(char **buffer)
// {
//   int container_header, container_size;
//   container_header = container_size;
//
//   EDNValue *container_value = malloc(sizeof(struct EDNValue));
//
//   memcpy(&container_header, *buffer, sizeof(uint32_t));
//
//   container_size = (container_header & COUNT_MASK);
//
//   // we probably want to extract this behaviour out into separate functions
//   // we might potentially need a function per type of collection
//   if((container_header & TYPE_MASK) == EDNB_MAP)
//   {
//     //we examined the header, so increase the number of bytes read
//     *buffer += sizeof(uint32_t);
//
//     container_value->type = EDN_MAP;
//     container_value->entries = malloc(sizeof(struct EDNValuePair));
//
//     for(int i = 0; i < container_size; i ++)
//     {
//       uint32_t key_header, value_header;
//       EDNValue *key, *value;
//
//       memcpy(&key_header, *buffer, sizeof(uint32_t));
//
//       if((key_header & TYPE_MASK) == EDNB_MAP)
//       {
//         key = EDNB_to_EDNValue(buffer);
//       }
//       else
//       {
//         //The key is a scalar type
//         int key_length = key_header & LENGTH_MASK;
//
//         //assume just strings for now
//         key = malloc(sizeof(struct EDNValue));
//         key->string = malloc(sizeof(char) * key_length);
//
//         *buffer += sizeof(uint32_t);
//
//         memcpy(key->string, *buffer, key_length);
//
//         *buffer += key_length;
//       }
//
//       container_value->entries[i].key = *key;
//
//       memcpy(&value_header, *buffer, sizeof(uint32_t));
//
//       if((value_header & TYPE_MASK) == EDNB_MAP)
//       {
//         value = EDNB_to_EDNValue(buffer);
//       }
//       else
//       {
//         //The value is a scalar type
//         int value_length = key_header & LENGTH_MASK;
//
//         //assume just strings for now
//         value = malloc(sizeof(struct EDNValue));
//         value->string = malloc(sizeof(char) * value_length);
//
//         *buffer += sizeof(uint32_t);
//
//         memcpy(value->string, *buffer, value_length);
//
//         *buffer += value_length;
//       }
//
//       container_value->entries[i].value = *value;
//
//     }
//   }
//
//   return container_value;
// }
