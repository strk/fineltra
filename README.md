[PostgreSQL](http://postgresql.org/) extension to translate
[PostGIS](http://www.postgis.net) geometries using the [FINELTRA algorithm](
http://www.swisstopo.admin.ch/internet/swisstopo/en/home/topics/survey/lv95/lv03-lv95/chenyx06.html
)

Development was funded by [Canton Solothurn (Switzerland)](
http://www.so.ch/verwaltung/bau-und-justizdepartement/amt-fuer-geoinformation/geoportal/
).

Code is released as free software under the terms of the [GPL license]
(COPYING), version 3 or later.


BUILDING
========

The code depends on the liblwgeom library shipped with PostGIS.
The library needs to be linked statically to avoid clashes with
different versions of it included in the PostGIS module itself,
which is dlopened by the PostgreSQL backend.

Once statically linked to liblwgeom (2.2.0+) the code is also
compatible with older PostGIS version (tested with 1.5 upward).

Building and installing should be as simple as:

  ./autogen.sh
  ./configure
  make

INSTALLING
==========

  sudo make install

