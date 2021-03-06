[![Build Status]
(https://secure.travis-ci.org/strk/fineltra.png)]
(http://travis-ci.org/strk/fineltra)


[PostgreSQL](http://postgresql.org/) extension to translate
[PostGIS](http://www.postgis.net) geometries using the [FINELTRA algorithm](
http://www.swisstopo.admin.ch/internet/swisstopo/en/home/topics/survey/lv95/lv03-lv95/chenyx06.html
)

Development was funded by [Canton Solothurn (Switzerland)](
http://www.so.ch/verwaltung/bau-und-justizdepartement/amt-fuer-geoinformation/geoportal/
).

Code is released as free software under the terms of the [GPL license]
(COPYING), version 3 or later.


DEPENDENCIES
============

Fineltra is a PostgreSQL extension, compatible with version 9.1
and later.

The code depends on the liblwgeom library shipped with PostGIS.
The library needs to be linked statically to avoid clashes with
different versions of it included in the PostGIS module itself,
which is dlopened by the PostgreSQL backend.

Once statically linked to liblwgeom (2.2.0+) the code is also
compatible with older PostGIS version (tested with 1.5 upward).

BUILDING
========

Building and installing should be as simple as:

```sh
  ./autogen.sh
  ./configure
  make
```

INSTALLING
==========

```sh
  sudo make install
```

TESTING
=======

Only post-install checks are supported at the moment. Run:

```sh
  make installcheck
```

USING
=====

Make sure to have the extension loaded in the database:

```sql
  CREATE EXTENSION fineltra
```

The extension provides a ``ST_Fineltra`` function that takes
a PostGIS Geometry object in EWKB form, the identifier of a table
containing reference triangles and the name of the columns containing
the source and the target triangles. It returns a PostGIS Geometry in
EWKB form representing the input geometry translated using the
appropriate triangle from the reference set.

Example usage:

```sql
  SELECT ST_Fineltra(
      geom,
      'chenyx06_triangles',
      'geom_lv03',
      'geom_lv95'
    );
```

The reference triangle columns need be of type POLYGON where each
polygon has exactly 4 vertices. Order of the vertices in the source
and the target column polygons must match.

The SRID of the source triangles must match the SRID of the input
geometry. The output geometry will have the SRID of the target
triangles.

It is highly recommended to create a GiST index on the source triangle
column as it will be queried for each call to the ``ST_Fineltra``
function. Example of creating such an index:

```sql
CREATE INDEX ON chenyx06_triangles USING GiST (geom_lv03);
```
