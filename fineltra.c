/***********************************************************************
 * fineltra.c
 *
 * Copyright (C) 2015 Sandro Santilli <strk@keybit.net>
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU General Public Licence version 3 or later.
 * See the COPYING file.
 *
 ***********************************************************************/

#include <assert.h>
#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "access/hash.h"
#include "utils/hsearch.h"
#include "fineltra_config.h"
#include "liblwgeom.h"

PG_MODULE_MAGIC;

#define PGC_ERRMSG_MAXLEN 256

static void *
pg_alloc(size_t size)
{
  void * result;
  result = palloc(size);
  if ( ! result )
  {
    ereport(ERROR, (errmsg_internal("Out of virtual memory")));
    return NULL;
  }
  return result;
}

static void *
pg_realloc(void *mem, size_t size)
{
  return repalloc(mem, size);
}

static void
pg_free(void *ptr)
{
  pfree(ptr);
}

#ifndef __GNUC__
# define __attribute__ (x)
#endif

static void pg_error(const char *fmt, va_list ap)
  __attribute__ (( format(printf, 1, 0) ));
static void pg_notice(const char *fmt, va_list ap)
  __attribute__ (( format(printf, 1, 0) ));
static void pg_debug(int level, const char *fmt, va_list ap)
  __attribute__ (( format(printf, 2, 0) ));

static void
pg_error(const char *fmt, va_list ap)
{
  char errmsg[PGC_ERRMSG_MAXLEN+1];

  vsnprintf (errmsg, PGC_ERRMSG_MAXLEN, fmt, ap);

  errmsg[PGC_ERRMSG_MAXLEN]='\0';
  ereport(ERROR, (errmsg_internal("%s", errmsg)));
}

static void
pg_notice(const char *fmt, va_list ap)
{
  char errmsg[PGC_ERRMSG_MAXLEN+1];

  vsnprintf (errmsg, PGC_ERRMSG_MAXLEN, fmt, ap);

  errmsg[PGC_ERRMSG_MAXLEN]='\0';
  ereport(NOTICE, (errmsg_internal("%s", errmsg)));
}

static void
pg_debug(int level, const char *fmt, va_list ap)
{
  char errmsg[PGC_ERRMSG_MAXLEN+1];
  int pglevel[6] = {NOTICE, DEBUG1, DEBUG2, DEBUG3, DEBUG4, DEBUG5};
  vsnprintf (errmsg, PGC_ERRMSG_MAXLEN, fmt, ap);
  errmsg[PGC_ERRMSG_MAXLEN]='\0';
  if ( level >= 0 && level <= 5 )
    ereport(pglevel[level], (errmsg_internal("%s", errmsg)));
  else
    ereport(DEBUG5, (errmsg_internal("%s", errmsg)));
}

void _PG_init(void);
void
_PG_init(void)
{
  elog(LOG, "Fineltra (%s) module loaded", FINELTRA_VERSION);

  lwgeom_set_handlers(pg_alloc, pg_realloc, pg_free, pg_error, pg_notice);
  lwgeom_set_debuglogger(pg_debug);
}

/* Module unload callback */
void _PG_fini(void);
void
_PG_fini(void)
{
  elog(LOG, "Fineltra (%s) module unloaded", FINELTRA_VERSION);
}

Datum st_fineltra(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(st_fineltra);
Datum st_fineltra(PG_FUNCTION_ARGS)
{
  bytea *bytea_wkb;
  uint8_t *wkb;
  LWGEOM *lwgeom;
  size_t wkb_size;

  /* 1. Read arguments */
  bytea_wkb = (bytea*)PG_GETARG_BYTEA_P(0); /* TODO: copy needed ? */
  wkb = (uint8_t*)VARDATA(bytea_wkb);
  lwgeom = lwgeom_from_wkb(wkb, VARSIZE(bytea_wkb)-VARHDRSZ, LW_PARSER_CHECK_ALL);
  if ( ! lwgeom )
  {
    elog(ERROR, "Failed to parse WKB");
    PG_RETURN_NULL();
  }


  /* 2. Fetch set of triangles overlapping input geometry */
  /* 3. For each vertex in input: */
  /* 3.1. Find source and target triangles from set */
  /* 3.2. Translate point from source to target */

  elog(WARNING, "ST_Fineltra not implemented yet, returning input untouched");

  wkb = lwgeom_to_wkb(lwgeom, WKB_EXTENDED, &wkb_size);

  bytea_wkb = palloc(wkb_size + VARHDRSZ);
  memcpy(VARDATA(bytea_wkb), wkb, wkb_size);
  SET_VARSIZE(bytea_wkb, wkb_size+VARHDRSZ);

  lwfree(wkb);
  PG_RETURN_BYTEA_P(bytea_wkb);
}
