# Polyvore integration

Mirage imports a bounded, reproducible sample of metadata from
[xthan/polyvore-dataset](https://github.com/xthan/polyvore-dataset), pinned to
`ba8aa71b6589e8920d172a8b5df84746be724d00`.
Archive SHA-256: `37c36efc67cabdaa58f2d73cbc1c5143d87f9918ff3d4da20e271e40c89e902b`.
The upstream README describes historical Polyvore data collected around 2017 and
notes that its original image URLs no longer work. Mirage downloads no images.

The repository declares Apache-2.0; its complete license is retained in `LICENSE`.
Attribution: Xintong Han, Zuxuan Wu, Yu-Gang Jiang and Larry S. Davis,
*Learning Fashion Compatibility with Bidirectional LSTMs*, ACM Multimedia 2017.
License scope recorded by Mirage: repository-declared metadata, excluding images.
This does not assert ownership or permissions for external linked images.

Modifications: category mapping, UTF-8/HTML/control-character normalization,
basic-category projection, omission counts, text deduplication, bounded sampling
and embeddings. No prices, popularity metrics, usernames, original descriptions,
image addresses or external shopping links are published by the retrieval API.

## Data selection

- Read only the archive's named JSON members in memory; never extract paths.
- Index only `train_no_dup.json` (17,316 source outfits).
- Select 128 records: 64 TOP+BOTTOM+FOOTWEAR and 64 ONE_PIECE+FOOTWEAR.
- Reject missing/ambiguous core categories, bad indices/IDs and empty core names.
- Omit accessories, outerwear and unmapped elements, retaining omission counts.
- Order eligible records by SHA-256 of set_id, alternate structures and deduplicate
  exact normalized core-item/category sequences.
- Keep 24 validation projections separately under ignored `out/`; never index them.
  Test and fill-in-the-blank datasets are not used as retrieval documents.

An example is an observed co-occurrence, **not a positive aesthetic-quality label**,
complete reconstruction of the original look, or evidence of a current trend.
The archive and its checksum, selected source IDs, counts, split and selection rule
are recorded in `manifest.json`. These external IDs are not user wardrobe UUIDs.

## Reproducible commands

From recommendation-service:

```powershell
python knowledge/import_polyvore.py --embed
python knowledge/import_polyvore.py --export-sql
```

The first command downloads only a fixed verified archive, normalizes the data,
prepares a corpus/holdout under `out/` and generates/reuses embeddings with the
existing separate NVIDIA embedding key. Model and dimensions are fixed to
`nvidia/nemotron-3.5-embed-1b`, 1024. Batches contain at most eight documents and
completed batches are cached. An unchanged rerun generated **zero** new embeddings.
The administrative `--limit` is bounded to 8–512, with 128 as default.

The second command is offline. It validates hashes/source/vector data and exports
an administrator-reviewable `out/polyvore-upsert.sql`. It uses escaped JSON
recordsets, upserts by external ID and updates only changed document hashes.
It does not connect to Neon, delete records or automatically retire old versions.
Apply reviewed statements as an administrator on `dev-vector-store`; migrations
and SQL imports must not use the application's SELECT-only role. The initial
128 records were imported through the connected Neon administrator.
No administrator URL or new API credential was added to the runtime `.env`.

## Runtime

Table: `recommendation.outfit_examples` in `recommendation_vectors` on
`dev-vector-store`. App role: SELECT only; forced RLS requires an authenticated
backend actor and allows only published train rows. Private wardrobe RLS is unchanged.

Generation flow: style references → private wardrobe candidates → Polyvore examples
filtered to available core structures → Nemotron → own-ID/constraint/revision validation.
If no complete category structure exists, the examples stage returns no examples
without a provider call; existing insufficiency handling applies. Retrieval errors
stop generation, while a valid empty result is not an error.

Inspection: `POST /api/v1/outfit-examples/search` with IAM Bearer token and:

```json
{"query":"camiseta con jean y botines","structure":"SEPARATES","limit":3}
```

`query` is 1–512 bytes, no NUL/blank-only; `structure` is empty, SEPARATES or
ONE_PIECE; limit is 1–3. User IDs, source URLs, styles and split selection are rejected.
The query result includes source/projection metadata and similarity, never vectors.
There is no new conversational tool: automatic context preparation already handles
these examples. Existing tools remain search_wardrobe, get_outfit, search_style_knowledge.

The model receives examples separately from `garments`; external names, IDs,
materials and colors cannot become inventory facts. New recommendation snapshots
retain `outfit_examples` as retrieved provenance, not proof that the model used it.

## Verification

- CTest: offline importer, typed JSON/source/category guards, HTTP/auth guards and
  existing recommendation regressions.
- `tests/polyvore_http.py --live-neon`: actual dev Neon with controlled HTTP
  providers; verifies structures, train-only retrieval, public sharing, own UUIDs,
  provenance, external-ID rejection and dependency failure. Cleans only reserved
  synthetic wardrobe actors, never the public example corpus.
- Actual restricted-role check: no actor returns zero rows; authenticated actor sees
  exactly the 128 selected train records, dimension 1024; no INSERT/UPDATE/DELETE.
- Real NVIDIA/Neon check: 8 source-document self-retrieval smoke queries had Hit@3=1.0;
  8 held-out validation queries returned train records of the requested structure
  without indexing their IDs. These are smoke/structural tests, **not a measured
  aesthetic quality or generalization score**.
- Real Nemotron generation used retrieved Polyvore context and current Closet UUIDs,
  respected no-white and persisted the same retrieved provenance. IAM, Closet,
  NVIDIA and Neon were real; chat persistence was a local HTTP double so no real
  user conversations were created. Private report: `out/polyvore-quality.json`.

Remaining quality and release work: `../../docs/RAG_COMPLETION.md`.
