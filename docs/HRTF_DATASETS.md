# HRTF Datasets

This document describes the HRTF datasets registered in
[`assets/hrtf/catalog.json`](../assets/hrtf/catalog.json).
It is auto-derived from the catalog — every entry in the catalog has a
corresponding row in the table below.

---

## Dataset Table

| Name | Display Name | Source URL | License | Attribution | Citation |
|------|-------------|-----------|---------|-------------|---------|
| `kemar` | MIT KEMAR (Gardner & Martin) | <https://sound.media.mit.edu/resources/KEMAR.html> | CC-BY-4.0 | Gardner, W.G. and Martin, K.D. (1994). HRTF Measurements of a KEMAR Dummy-Head Microphone. MIT Media Lab | Gardner & Martin 1994, MIT Media Lab |
| `cipic_003` | CIPIC Subject 003 (UC Davis) | <https://www.ece.ucdavis.edu/cipic/spatial-sound/hrtf-data/> | academic-research-only | Algazi, V.R., Duda, R.O., Thompson, D.M., Avendano, C. (2001). The CIPIC HRTF Database. IEEE WASPAA. UC Davis CIPIC Interface Lab | Algazi et al. 2001, IEEE WASPAA |
| `sadie2_H08` | SADIE-II Subject H08 (University of York) | <https://www.york.ac.uk/sadie-ii/database.html> | CC-BY-4.0 | Armstrong, C., Threshold, L., Murphy, D., Kearney, G. (2018). A Perceptual Evaluation of Individual and Non-Individual HRTFs: A Case Study of the SADIE II Database. Applied Sciences. University of York | Armstrong et al. 2018, Applied Sciences |
| `hutubs_pp1` | HUTUBS Subject pp1 (TU Berlin) | <https://depositonce.tu-berlin.de/handle/11303/10294> | academic-research-only | Brinkmann, F. et al. (2019). A Cross-Evaluated Database of Measured and Simulated HRTFs Including 3D Head Meshes, Anthropometric Features, and Headphone Impulse Responses. JAES. TU Berlin | Brinkmann et al. 2019, JAES |

---

## Auto-Fetch vs. --accept-license Policy

### Default: CC-BY auto-fetch

The fetch script (`scripts/fetch_hrtf_datasets.sh`) automatically downloads
only entries that satisfy **both** conditions:

- `auto_fetch: true` in the catalog, **and**
- `license` starts with `CC-BY`

Currently auto-fetched datasets: **kemar**, **sadie2_H08**

### Non-CC-BY datasets: explicit consent required

Datasets with `academic-research-only` or other non-CC-BY licenses are
**skipped by default**. The script prints a notice explaining what flag is
needed. To fetch one, pass `--accept-license=<name>`:

```bash
# Fetch CIPIC (academic-research-only — ensure you comply with the license)
bash scripts/fetch_hrtf_datasets.sh --accept-license=cipic_003

# Fetch HUTUBS
bash scripts/fetch_hrtf_datasets.sh --accept-license=hutubs_pp1

# Fetch multiple non-CC-BY datasets in one run
bash scripts/fetch_hrtf_datasets.sh \
  --accept-license=cipic_003 \
  --accept-license=hutubs_pp1
```

User consent for SADIE-II / CIPIC / HUTUBS has been granted per project plan
§271, but the `--accept-license` flag is still required at run-time so that
the intent is always explicit in CI or shell history.

### Dry-run (URL health check)

```bash
bash scripts/fetch_hrtf_datasets.sh --dry-run
```

This performs an HTTP HEAD request for every catalog URL and reports the HTTP
status code without downloading anything. Useful for CI URL-rot detection.
A URL that is unreachable is reported as `UNREACHABLE` but does **not** cause
the script to exit non-zero — all URLs are always checked.

---

## Running a Full CC-BY Fetch

```bash
# Fetch all CC-BY datasets (kemar + sadie2_H08) and convert to .speh
bash scripts/fetch_hrtf_datasets.sh
```

Downloaded `.sofa` files land in `assets/hrtf/raw/` (gitignored).
Converted `.speh` files land in `assets/hrtf/` (also gitignored — binaries
are never committed; only `catalog.json` and this doc are version-controlled).

---

## Attribution Notes

All datasets must be credited in any publication or derivative work that uses
spatial audio features of this engine. The `attribution` and citation
information in the table above (and in `catalog.json`) are the canonical
references.
