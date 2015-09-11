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

PG_MODULE_MAGIC;

void _PG_init(void);
void
_PG_init(void)
{
  elog(LOG, "Fineltra (%s) module loaded", FINELTRA_VERSION);
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

  /* 1. Read arguments */
  /* 2. Fetch set of triangles overlapping input geometry */
  /* 3. For each vertex in input: */
  /* 3.1. Find source and target triangles from set */
  /* 3.2. Translate point from source to target */

  elog(ERROR, "ST_Fineltra not implemented yet");
  PG_RETURN_NULL();
}
