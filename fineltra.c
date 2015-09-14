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


typedef struct FIN_TRIANGLE_T {
  POINT2D t1;
  POINT2D t2;
  POINT2D t3;
} FIN_TRIANGLE;

typedef struct FIN_TRIANGLE_PAIR_T {
  FIN_TRIANGLE src;
  FIN_TRIANGLE tgt;
} FIN_TRIANGLE_PAIR;

typedef struct FIN_TRISET_T {
  int num;
  FIN_TRIANGLE_PAIR *pair;
} FIN_TRISET;

static int
fin_polygon_to_triangle(LWPOLY *poly, FIN_TRIANGLE *tri)
{
  POINTARRAY *pa;

  pa = poly->rings[0];
  if ( pa->npoints < 3 ) {
    elog(ERROR, "Too few points in triangle polygon: %d", pa->npoints);
    return 0;
  }
  getPoint2d_p(pa, 0, &tri->t1);
  getPoint2d_p(pa, 1, &tri->t2);
  getPoint2d_p(pa, 2, &tri->t3);
  return 1;
}

static int
fin_datum_to_triangle(Datum dat, FIN_TRIANGLE *tri)
{
  bytea *bytea_wkb;
  uint8_t *wkb;
  LWGEOM *lwgeom;
  LWPOLY *lwpoly;

  bytea_wkb = DatumGetByteaP( dat );
  wkb = (uint8_t*)VARDATA(bytea_wkb);
  lwgeom = lwgeom_from_wkb(wkb, VARSIZE(bytea_wkb)-VARHDRSZ, LW_PARSER_CHECK_ALL);
  lwpoly = lwgeom_as_lwpoly(lwgeom);
  if ( ! lwpoly )
  {
    elog(ERROR, "Non-polygon source triangle found");
    lwgeom_free(lwgeom);
    return 0;
  }
  if ( ! fin_polygon_to_triangle(lwpoly, tri) )
  {
    lwgeom_free(lwgeom);
    return 0;
  }
  lwgeom_free(lwgeom);
  return 1;
}

static void
fin_triset_destroy(FIN_TRISET *set)
{
  if ( set->num ) lwfree(set->pair);
  lwfree(set);
}

/* Return lwalloc'ed hexwkb representation for a GBOX */
static char *
_box2d_to_hexwkb(const GBOX *bbox, int srid)
{
  POINTARRAY *pa = ptarray_construct(0, 0, 2);
  POINT4D p;
  LWLINE *line;
  char *hex;
  size_t sz;

  p.x = bbox->xmin;
  p.y = bbox->ymin;
  ptarray_set_point4d(pa, 0, &p);
  p.x = bbox->xmax;
  p.y = bbox->ymax;
  ptarray_set_point4d(pa, 1, &p);
  line = lwline_construct(srid, NULL, pa);
  hex = lwgeom_to_hexwkb( lwline_as_lwgeom(line), WKT_EXTENDED, &sz);
  lwline_free(line);
  assert(hex[sz-1] == '\0');
  return hex;
}

static const char *
oid_to_tablename(Oid toid)
{
  StringInfoData sqldata;
  StringInfo sql = &sqldata;

  /* TODO: lookup cache in finfo ? */
  initStringInfo(sql);
  appendStringInfo(sql, "SELECT n.nspname, c.relname FROM "
    "pg_catalog.pg_namespace n, pg_catalog.pg_class c "
    "WHERE c.oid = %d AND n.oid = c.relnamespace",
    toid);

  return "chenyx06_triangles"; /* XXX TODO: fix this! */
}

static FIN_TRISET *
load_triangles(Oid toid, char *fsrc, char *ftgt, const LWGEOM* ext)
{
  const char *tname_tri;
  char *hexbox;
  int spi_result;
  StringInfoData sql;
  FIN_TRISET *ret;
  int i;
  const GBOX *box;

  tname_tri = oid_to_tablename(toid);

  box = lwgeom_get_bbox(ext);
  if ( ! box ) {
    /* Handle emptyness before calling this function */
    elog(ERROR, "The input geometry is empty");
    return NULL;
  }
  hexbox = _box2d_to_hexwkb(box, ext->srid);

  initStringInfo(&sql);
  appendStringInfo(&sql, "SELECT \"%s\"::bytea src,\"%s\"::bytea tgt "
                         "FROM %s "
                         "WHERE ST_Intersects(\"%s\", '%s'::geometry)",
                         fsrc, ftgt, tname_tri, fsrc, hexbox);
  lwfree(hexbox);

  if ( SPI_OK_CONNECT != SPI_connect() ) {
    elog(ERROR, "Could not connect to SPI");
    return NULL;
  }

  spi_result = SPI_execute(sql.data, true, 0);
  if ( spi_result != SPI_OK_SELECT ) {
    elog(ERROR, "unexpected return (%d) from query execution: %s", spi_result, sql.data);
    pfree(sql.data);
    return NULL;
  }
  pfree(sql.data);

  if ( ! SPI_processed )
  {
    elog(ERROR, "The input geometry does not intersect any source triangle");
    return NULL;
  }

  ret = palloc(sizeof(FIN_TRISET));
  ret->num = SPI_processed;
  ret->pair = palloc(sizeof(FIN_TRIANGLE_PAIR)*SPI_processed);
  for (i=0; i<SPI_processed; ++i)
  {
    Datum dat;
    bool isnull;
    FIN_TRIANGLE_PAIR *pair = ret->pair+i;

    /* src */
    dat = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc,
                        1, &isnull);
    assert ( ! isnull ); /* or it should have failed overlap test */
    if ( ! fin_datum_to_triangle(dat, &(pair->src)) ) return NULL;

    /* tgt */
    dat = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc,
                        2, &isnull);
    if ( isnull )
    {
      elog(ERROR, "Null target triangle in transformation table");
      fin_triset_destroy(ret);
      return NULL;
    }
    if ( ! fin_datum_to_triangle(dat, &(pair->tgt)) ) return NULL;
  }

  SPI_finish();

  return ret;
}

Datum st_fineltra(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(st_fineltra);
Datum st_fineltra(PG_FUNCTION_ARGS)
{
  bytea *bytea_wkb;
  uint8_t *wkb;
  LWGEOM *lwgeom;
  size_t wkb_size;
  Oid toid;
  Name fname_src, fname_tgt;
  FIN_TRISET *triangles;

  /* 1. Read arguments */
  bytea_wkb = (bytea*)PG_GETARG_BYTEA_P(0); /* TODO: copy needed ? */
  wkb = (uint8_t*)VARDATA(bytea_wkb);
  lwgeom = lwgeom_from_wkb(wkb, VARSIZE(bytea_wkb)-VARHDRSZ, LW_PARSER_CHECK_ALL);
  if ( ! lwgeom )
  {
    elog(ERROR, "Failed to parse (E)WKB");
    PG_RETURN_NULL();
  }

  if ( lwgeom_is_empty(lwgeom) )
  {
    /* nothing to transform here */
    return PG_GETARG_DATUM(0);
  }

  toid = PG_GETARG_OID(1);
  fname_src = PG_GETARG_NAME(2);
  fname_tgt = PG_GETARG_NAME(3);

  /* 2. Fetch set of triangles overlapping input geometry */
  triangles = load_triangles(toid, fname_src->data, fname_tgt->data, lwgeom);

  /* 3. For each vertex in input: */
  /* 3.1. Find source and target triangles from set */
  /* 3.2. Translate point from source to target */

  elog(WARNING, "ST_Fineltra not implemented yet, returning input untouched");

  wkb = lwgeom_to_wkb(lwgeom, WKB_EXTENDED, &wkb_size);

  bytea_wkb = palloc(wkb_size + VARHDRSZ);
  memcpy(VARDATA(bytea_wkb), wkb, wkb_size);
  SET_VARSIZE(bytea_wkb, wkb_size+VARHDRSZ);

  lwfree(wkb);
  fin_triset_destroy(triangles);

  PG_RETURN_BYTEA_P(bytea_wkb);
}
