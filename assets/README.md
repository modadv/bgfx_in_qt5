# Assets

This repository keeps the tracked asset footprint intentionally small.

## Tracked Sample Assets

The repository includes a minimal public sample subset for first-run validation:

- `assets/benchmarks/terrain/usgs_3dep/mt_st_helens_0256.png`
- `assets/benchmarks/terrain/usgs_3dep/mt_st_helens_1024.png`
- `assets/benchmarks/terrain/usgs_3dep/manifest.json`

These files are enough to verify that:

- the Qt example starts
- the benchmark preset list resolves
- the standard heightmap loading path works

## Optional Full Benchmark Pack

The full benchmark pack is intentionally not committed because it is too large
for a lightweight source repository.

To fetch the full public benchmark set from USGS 3DEP, run:

```bash
python scripts/fetch_usgs_3dep_assets.py
```

The script reads the public manifest, downloads the direct source rasters, and
reconstructs the larger derived images. ImageMagick 7 is required for raster
conversion and resize operations.

## Notes

- The benchmark manifest can contain entries whose local files are missing.
  The Qt example skips those entries instead of failing.
- If you want a different benchmark region, treat the shipped manifest as a
  template and regenerate the files with your own public DEM source URLs.
