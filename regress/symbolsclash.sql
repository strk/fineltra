-- This is a test showing how dynamically linking to liblwgeom-2.2
-- and running against a PostGIS-1.5 module (which statically links to
-- liblwgeom-1.5) results in a crash.
--
--
-- The output I get in those conditions is:
--   LOG:  Fineltra (0.0.0dev) module loaded
--   SSL SYSCALL error: EOF detected
--   connection to server was lost
--
-- The "correct" run would instead output:
--   column "x" does not exist
--
-- You can verify this by reverting static linking:
--  git revert add6f39052da759bbc9844793b926e28b32ff324
--
-- Beside static linking, another fix would be building PostGIS-1.5
-- so that it does NOT export symbols from the libraries it links to,
-- see https://trac.osgeo.org/postgis/ticket/3281
-- and https://github.com/postgis/postgis/pull/65
-- (aka `--exclude-libs ALL`)
--
select ST_Fineltra(
      ST_AsEWKB('SRID=21781;POINT(620759.050860082 264806.29)'::geometry),
 'spatial_ref_sys','x','x');
