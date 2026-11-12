# Style corpus: provenance and license

`style_corpus.json` contains eight Spanish fragments: short adaptations of the
linked Wikipedia concepts plus explicitly labelled Mirage editorial guidance.
It is a small initial corpus, not a professional fashion dataset or a trend feed.
No images or full articles were imported.

All corpus text is provided under **CC BY-SA 4.0**:
https://creativecommons.org/licenses/by-sa/4.0/
Attribution: Wikipedia contributors for the linked source pages; Spanish
adaptation and editorial additions by Mirage. Changes: summarization,
translation and application to garment recommendations. Source URLs and that
attribution travel with each retrieved fragment. The content license applies to
this corpus, not automatically to the application code.

Sources consulted on 2026-11-11:

- Monochrome, including the monochromatic color scheme section:
  https://en.wikipedia.org/wiki/Monochromatic_color
- Complementary colors: https://en.wikipedia.org/wiki/Complementary_colors
- Smart casual: https://en.wikipedia.org/wiki/Smart_casual
- Streetwear: https://en.wikipedia.org/wiki/Streetwear
- Capsule wardrobe: https://en.wikipedia.org/wiki/Capsule_wardrobe
- Layered clothing: https://en.wikipedia.org/wiki/Layered_clothing

Each Wikipedia page links its contributors/history and content license. General
facts are separated from Mirage editorial suggestions by the wording of each
fragment and `content_kind=adaptation_with_editorial_guidance`. These are
suggestions, not universal rules, source claims about a user's garments, or
professional endorsement. Recheck sources when revising the corpus.

## Administrative preparation

`python knowledge/prepare_embeddings.py` reads the reviewed file and the separate
NVIDIA embedding configuration from `.env`. It calls only NVIDIA's official
embedding endpoint, validates finite nonzero vectors of dimension 1024, and saves
prepared data in ignored `out/style-knowledge-vectors.json`. It reuses that local
output when document hashes match. It never prints keys or writes to Neon.

Apply `neon/migrations/002_style_knowledge.sql` through an administrator on the
prepared development branch. An administrator imports the reviewed prepared
rows from a reviewed JSON recordset and an upsert by `id`; update only when
`document_hash` differs. No deletion or broad automatic pruning is needed. Record
revisions and review new sources and their licenses before publishing. Retire an
old reference explicitly with `published=false`; runtime credentials cannot do it.

The current eight real embeddings were imported into `dev-vector-store` through
the connected Neon administrator, without changing `main` or private wardrobe rows.
The application role has SELECT only. There is no public ingestion endpoint,
model-controlled URL downloader or model-controlled corpus editor.
