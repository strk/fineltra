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

#define ABS(x) (x<0?-x:x)

#undef FIN_DEBUG

#ifdef FIN_DEBUG
static void
debug_wkb(uint8_t *wkb, size_t wkb_size)
{
  size_t i;
  char *buf, *ptr;
  buf = lwalloc(wkb_size*2+1);
  ptr = buf;
  for (i=0; i<wkb_size; ++i)
  {
    deparse_hex(wkb[i], ptr);
    ptr+=2;
  }
  *ptr = '\0';
  elog(DEBUG1, "WKB(%d): %s", wkb_size, buf);
  lwfree(buf);
}
#endif


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
#ifdef FIN_DEBUG
  /* NOTE: this is not necessarely present, was added in liblwgeom 2.2.0 */
  lwgeom_set_debuglogger(pg_debug);
#endif
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
  int srid_src;
  int srid_tgt;
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
fin_datum_to_triangle(Datum dat, FIN_TRIANGLE *tri, int *srid)
{
  bytea *bytea_wkb;
  uint8_t *wkb;
  LWGEOM *lwgeom;
  LWPOLY *lwpoly;

  bytea_wkb = DatumGetByteaP( dat );
  wkb = (uint8_t*)VARDATA(bytea_wkb);

#ifdef FIN_DEBUG
  debug_wkb(wkb, VARSIZE(bytea_wkb)-VARHDRSZ);
#endif

  lwgeom = lwgeom_from_wkb(wkb, VARSIZE(bytea_wkb)-VARHDRSZ, LW_PARSER_CHECK_ALL);
  *srid = lwgeom->srid;
  lwpoly = lwgeom_as_lwpoly(lwgeom);
  if ( ! lwpoly )
  {
    elog(ERROR, "Non-polygon triangle found (%s)", lwtype_name(lwgeom_get_type(lwgeom)));
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

static FIN_TRISET *
fin_triset_create(size_t numtriangles)
{
  FIN_TRISET *ret;
  ret = palloc(sizeof(FIN_TRISET));
  ret->num = numtriangles;
  ret->srid_src = ret->srid_tgt = 0;
  ret->pair = palloc(sizeof(FIN_TRIANGLE_PAIR)*numtriangles);
  return ret;
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

#define MAXTABLENAME 1024

typedef struct CacheItemNameByOid_t
{
  Oid oid;
  char tname[MAXTABLENAME];
}
CacheItemNameByOid;


typedef struct {
  CacheItemNameByOid nameByOid;
} FineltraCache;

/**
* Utility function to read the upper memory context off a function
* call
* info data.
*/
static MemoryContext
FIContext(FunctionCallInfoData* fcinfo)
{
  return fcinfo->flinfo->fn_mcxt;
}


/* return 1 if a cache for given oid existed, 0 otherwise */
static int
getNameByOidCache(FunctionCallInfo fcinfo, Oid toid, const char **nam)
{
  CacheItemNameByOid *cache = (CacheItemNameByOid*) fcinfo->flinfo->fn_extra;
  if ( ! cache ) return 0;
  if ( cache->oid != toid ) {
    elog(WARNING, "Table name cache is being ineffective");
    return 0;
  }
  *nam = cache->tname;
  return 1;
}

/* set cache by oid */
static void
setNameByOidCache(FunctionCallInfo fcinfo, Oid toid, const char *nam)
{
  CacheItemNameByOid *cache = (CacheItemNameByOid*) fcinfo->flinfo->fn_extra;
  if ( ! cache ) {
    cache = fcinfo->flinfo->fn_extra = MemoryContextAlloc(
        FIContext(fcinfo),
        sizeof(CacheItemNameByOid)
    );
  }
  cache->oid = toid;
  strncpy(cache->tname, nam, MAXTABLENAME);
}

/* NOTE: it is expected that the caller invoked SPI_connect already */
static const char *
oid_to_tablename(FunctionCallInfo fcinfo, Oid toid)
{
  StringInfoData sql;
  int spi_result;
  static char buf[MAXTABLENAME]; /* TODO: use cache */
  const char *namtab, *namsch;

  if ( getNameByOidCache(fcinfo, toid, &namtab) )
  {
    return namtab;
  }

  initStringInfo(&sql);
  appendStringInfo(&sql, "SELECT n.nspname, c.relname FROM "
    "pg_catalog.pg_namespace n, pg_catalog.pg_class c "
    "WHERE c.oid = %d AND n.oid = c.relnamespace",
    toid);

  spi_result = SPI_execute(sql.data, true, 0);
  if ( spi_result != SPI_OK_SELECT ) {
    elog(ERROR, "unexpected return (%d) from query execution: %s", spi_result, sql.data);
    pfree(sql.data);
    return NULL;
  }

  if ( ! SPI_processed )
  {
    elog(ERROR, "Triangle table regclass (%d) does not exist", toid);
    return NULL;
  }

  namsch = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
  namtab = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2);
  if ( MAXTABLENAME <= snprintf(buf, MAXTABLENAME, "\"%s\".\"%s\"", namsch, namtab) )
  {
    elog(ERROR, "Table name too long"); /* FIXME */
    return NULL;
  }

  setNameByOidCache(fcinfo, toid, buf);

  return buf;
}

static FIN_TRISET *
load_triangles(FunctionCallInfo fcinfo, Oid toid, char *fsrc, char *ftgt, const LWGEOM* ext)
{
  const char *tname_tri;
  char *hexbox;
  int spi_result;
  StringInfoData sql;
  FIN_TRISET *ret;
  int i;
  const GBOX *box;
  MemoryContext oldcontext;

  if ( SPI_OK_CONNECT != SPI_connect() ) {
    elog(ERROR, "Could not connect to SPI");
    return NULL;
  }

  tname_tri = oid_to_tablename(fcinfo, toid);
  if ( ! tname_tri ) return NULL;

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
                         "WHERE \"%s\" && '%s'::geometry",
                         fsrc, ftgt, tname_tri, fsrc, hexbox);
  lwfree(hexbox);

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

  oldcontext = MemoryContextSwitchTo(FIContext(fcinfo));
  ret = fin_triset_create(SPI_processed);
  for (i=0; i<SPI_processed; ++i)
  {
    Datum dat;
    bool isnull;
    FIN_TRIANGLE_PAIR *pair = ret->pair+i;

    /* src */
    dat = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc,
                        1, &isnull);
    assert ( ! isnull ); /* or it should have failed overlap test */
    if ( ! fin_datum_to_triangle(dat, &(pair->src), &ret->srid_src) )
      return NULL;

    /* tgt */
    dat = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc,
                        2, &isnull);
    if ( isnull )
    {
      elog(ERROR, "Null target triangle in transformation table");
      fin_triset_destroy(ret);
      return NULL;
    }
    if ( ! fin_datum_to_triangle(dat, &(pair->tgt), &ret->srid_tgt) )
      return NULL;
  }
  MemoryContextSwitchTo( oldcontext );

  if ( ret->srid_src != ext->srid )
  {
    elog(ERROR, "Source triangle SRID (%d) not same as geometry SRID (%d)",
                ret->srid_src, ext->srid);
    fin_triset_destroy(ret);
    return NULL;
  }

  SPI_finish();

  return ret;
}

/**
* lw_segment_side()
*
* Return -1  if point Q is left of segment P
* Return  1  if point Q is right of segment P
* Return  0  if point Q is on or collinear to segment P
*/
static int
fin_ptside(const POINT2D *p1, const POINT2D *p2, const POINT2D *q)
{
  int ret;
  double side = ( (q->x - p1->x) * (p2->y - p1->y) - (p2->x - p1->x) * (q->y - p1->y) );
  ret = side < 0 ? -1 : side > 0 ? 1 : 0;
#ifdef FIN_DEBUG
  elog(DEBUG1, "Side of POINT(%.15g %.15g) on "
               "LINESTRING(%.15g %.15g, %.15g %.15g) is %g (%d)",
               q->x, q->y, p1->x, p1->y, p2->x, p2->y, side, ret);
#endif
  return ret;
}

static int
fin_triangle_covers_point(FIN_TRIANGLE *tri, POINT2D *pt)
{
  /* side of triangle, cache this in FIN_TRIANGLE ? */
  int triside = fin_ptside(&tri->t1, &tri->t2, &tri->t3);
  int side;

  side = fin_ptside(&tri->t1, &tri->t2, pt);
  if ( ! side ) return 1; /* on t1-t2 segment */
  if ( side != triside ) return 0; /* wrong side */

  side = fin_ptside(&tri->t2, &tri->t3, pt);
  if ( ! side ) return 1; /* on t2-t3 segment */
  if ( side != triside ) return 0; /* wrong side */

  side = fin_ptside(&tri->t3, &tri->t1, pt);
  if ( ! side ) return 1; /* on t3-t1 segment */
  if ( side != triside ) return 0; /* wrong side */

  return 1;
}

static FIN_TRIANGLE_PAIR *
fin_find_triangle(FIN_TRISET *set, POINT2D *pt)
{
  int i;
  for (i=0; i<set->num; ++i)
  {
    FIN_TRIANGLE_PAIR *pair = set->pair+i;
    FIN_TRIANGLE *src = &(pair->src);
    int covers = fin_triangle_covers_point(src, pt);

#ifdef FIN_DEBUG
    elog(DEBUG1, "Triangle LINESTRING(%.15g %.15g, %.15g %.15g, %.15g %.15g)"
                 " covers POINT(%.15g %.15g) ? %d",
                 src->t1.x, src->t1.y,
                 src->t2.x, src->t2.y,
                 src->t3.x, src->t3.y,
                 pt->x, pt->y, covers);
#endif

    /* Both inside and on boundary are good */
    if ( covers ) return pair;
  }
  return NULL;
}

static void
fin_transform_point(POINT2D *from, POINT4D *to, FIN_TRIANGLE_PAIR *pair)
{
  FIN_TRIANGLE *src = &(pair->src);
  FIN_TRIANGLE *tgt = &(pair->tgt);
  POINT2D v1;
  POINT2D v2;
  POINT2D v3;

  /* translation vectors for each point, source to target.
   * NOTE: could be cached in TRIANGLE_PAIR */

  v1.x = tgt->t1.x - src->t1.x;
  v1.y = tgt->t1.y - src->t1.y;

  v2.x = tgt->t2.x - src->t2.x;
  v2.y = tgt->t2.y - src->t2.y;

  v3.x = tgt->t3.x - src->t3.x;
  v3.y = tgt->t3.y - src->t3.y;

  {

  /* areas of the three subtriangles, Fineltra Manual 4-5 */

  /* P1 is opposite of t1 */
  double P1 = ABS( 0.5 * ( from->x * (src->t2.y - src->t3.y) + src->t2.x * ( src->t3.y - from->y ) + src->t3.x * ( from->y - src->t2.y ) ) ) ;
  /* P2 is opposite of t2 */
  double P2 = ABS( 0.5 * ( from->x * (src->t1.y - src->t3.y) + src->t1.x * ( src->t3.y - from->y ) + src->t3.x * ( from->y - src->t1.y ) ) ) ;
  /* P3 is opposite of t3 */
  double P3 = ABS( 0.5 * ( from->x * (src->t1.y - src->t2.y) + src->t1.x * ( src->t2.y - from->y ) + src->t2.x * ( from->y - src->t1.y ) ) ) ;

  /* NOTE: could be cached as this is the area of src triangle */
  double PT = P1 + P2 + P3;

  /* Final interpolation */
  double dx = (v1.x*P1 + v2.x*P2 + v3.x*P3) / PT;
  double dy = (v1.y*P1 + v2.y*P2 + v3.y*P3) / PT;

  to->x = from->x + dx;
  to->y = from->y + dy;

  }
}

static int
ptarray_fineltra(POINTARRAY *pa, FIN_TRISET *triangles)
{
  int i;

  /* For each vertex in input: */
  for (i=0; i<pa->npoints; ++i)
  {
    POINT2D pt;
    POINT4D pt4d;
    FIN_TRIANGLE_PAIR *pair;

    /* Find source and target triangles from set */
    getPoint2d_p(pa, i, &pt);
    pair = fin_find_triangle(triangles, &pt);
    if ( ! pair )
    {
      elog(ERROR, "Input vertex (%.15g %.15g) "
                  "found outside all source triangles", pt.x, pt.y);
      return 0;
    }
  
#ifdef FIN_DEBUG
    elog(DEBUG1, "Triangle LINESTRING(%.15g %.15g, %.15g %.15g, %.15g %.15g)"
                 " contains POINT(%.15g %.15g)",
                 pair->src.t1.x, pair->src.t1.y,
                 pair->src.t2.x, pair->src.t2.y,
                 pair->src.t3.x, pair->src.t3.y,
                 pt.x, pt.y);
#endif
    fin_transform_point(&pt, &pt4d, pair);
    ptarray_set_point4d(pa, i, &pt4d);
  }

  return 1;
}

static int
lwgeom_fineltra(LWGEOM *geom, FIN_TRISET *triangles)
{
  int i;

  /* No points to transform in an empty! */
  if ( lwgeom_is_empty(geom) )
    return 1;

  switch(geom->type)
  {
    case POINTTYPE:
    case LINETYPE:
    case CIRCSTRINGTYPE:
    case TRIANGLETYPE:
    {
      LWLINE *g = (LWLINE*)geom;
      if ( ! ptarray_fineltra(g->points, triangles) ) return 0;
      break;
    }
    case POLYGONTYPE:
    {
      LWPOLY *g = (LWPOLY*)geom;
      for ( i = 0; i < g->nrings; i++ )
      {
        if ( ! ptarray_fineltra(g->rings[i], triangles) ) return 0;
      }
      break;
    }
    case MULTIPOINTTYPE:
    case MULTILINETYPE:
    case MULTIPOLYGONTYPE:
    case COLLECTIONTYPE:
    case COMPOUNDTYPE:
    case CURVEPOLYTYPE:
    case MULTICURVETYPE:
    case MULTISURFACETYPE:
    case POLYHEDRALSURFACETYPE:
    case TINTYPE:
    {
      LWCOLLECTION *g = (LWCOLLECTION*)geom;
      for ( i = 0; i < g->ngeoms; i++ )
      {
        if ( ! lwgeom_fineltra(g->geoms[i], triangles) ) return 0;
      }
      break;
    }
    default:
    {
      elog(ERROR, "lwgeom_fineltra: Cannot handle type '%s'",
                lwtype_name(geom->type));
      return 0;
    }
  }

  /* Update SRID to the target triangles in set */
  geom->srid = triangles->srid_tgt;

  return 1;
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
  triangles = load_triangles(fcinfo, toid, fname_src->data, fname_tgt->data, lwgeom);
  if ( ! triangles ) PG_RETURN_NULL();

#ifdef FIN_DEBUG
  elog(DEBUG1, "Fetched %d triangles intersecting input geometry", triangles->num);
#endif

  if ( ! lwgeom_fineltra(lwgeom, triangles) )
  {
    elog(ERROR, "lwgeom_fineltra failed");
    PG_RETURN_NULL();
  }

  wkb = lwgeom_to_wkb(lwgeom, WKB_EXTENDED, &wkb_size);

  bytea_wkb = palloc(wkb_size + VARHDRSZ);
  memcpy(VARDATA(bytea_wkb), wkb, wkb_size);
  SET_VARSIZE(bytea_wkb, wkb_size+VARHDRSZ);

  lwfree(wkb);
  fin_triset_destroy(triangles);

  PG_RETURN_BYTEA_P(bytea_wkb);
}
