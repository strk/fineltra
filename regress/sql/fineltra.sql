--set client_min_messages to ERROR;

CREATE TABLE triangles (src,tgt) AS VALUES
 ( 'SRID=1;POLYGON((0 0,1 1,2 0,0 0))'::geometry,
   'SRID=2;POLYGON((1 -1,2 1,3 -1,1 -1))'::geometry ) 
;

CREATE FUNCTION test (g geometry) RETURNS text
LANGUAGE 'plpgsql' STABLE STRICT
AS $$
DECLARE
  g2 GEOMETRY;
  g3 GEOMETRY;
BEGIN
  g2 := ST_GeomFromEWKB(
          ST_Fineltra(ST_AsEWKB(g), 'triangles', 'src', 'tgt')
        );
  g3 := ST_GeomFromEWKB(
          ST_Fineltra(ST_AsEWKB(g2), 'triangles', 'tgt', 'src')
        );
  RETURN ST_AsEWKT(g) || E'\n' ||
        ST_AsEWKT(g2) || E'\n' ||
        ST_AsEWKT(g3) || E'\n' ||
        -- we use AsEwkt to avoid drifts
        (ST_AsEWKT(g) = ST_AsEwkt(g3))::text;
END;
$$;

SELECT test( 'SRID=1;POINT(0 0)'::geometry ); -- point on vertex 1
SELECT test( 'SRID=1;POINT(1 1)'::geometry ); -- point on vertex 2
SELECT test( 'SRID=1;POINT(2 0)'::geometry ); -- point on vertex 3
SELECT test( 'SRID=1;POINT(0.5 0.5)'::geometry ); -- point on edge 1-2
SELECT test( 'SRID=1;POINT(1.5 0.5)'::geometry ); -- point on edge 2-3
SELECT test( 'SRID=1;POINT(0.5 0.0)'::geometry ); -- point on edge 3-1
SELECT test( 'SRID=1;POINT(1.0 0.5)'::geometry ); -- point on center
SELECT test( 'SRID=1;LINESTRING(1 1,0 0,2 0)'::geometry ); -- line around boundary

-- circular string around boundary
SELECT test( 'SRID=1;CIRCULARSTRING(2 0, 1 1, 0 0)'::geometry );

-- curve polygon around boundary
SELECT test( 'SRID=1;CURVEPOLYGON(COMPOUNDCURVE(
              CIRCULARSTRING(1 1,2 0,0 0),
              (0 0,1 1)))'::geometry );

-- geometry collection
SELECT test( 'SRID=1;GEOMETRYCOLLECTION(
              LINESTRING(0.2 0, 0.6 0),
              POLYGON((0.3 0.2,1 0.8,1.8 0.2,0.3 0.2),
                      (0.6 0.4,1 0.6,1.6 0.2,0.6 0.4)),
              POINT(1 0))'::geometry ) ;

-- triangle 
SELECT test( 'SRID=1;TRIANGLE((
              0.2 0.1,0.3 0.2,0.4 0.1,0.2 0.1
              ))'::geometry ) ;

-- tin 
SELECT test( 'SRID=1;TIN(
              ((0.2 0.2,0.3 0.3,0.4 0.2,0.2 0.2)),
              ((0.4 0.2,0.2 0.2,0.3 0.1,0.4 0.2))
             )'::geometry ) ;

DROP FUNCTION test (g geometry);
