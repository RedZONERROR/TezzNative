# TEZZAI v1

`TEZZAI` is the clean-from-zero AI stack for TezzNative.

## Scope in this baseline

- Local training data pipeline (`samples.tnxb`).
- `TezzModel` init/train/infer runtime on top of `lib/llm_core.tn`.
- Corpus indexing + codebook generation.
- Code generation CLI lane.
- Local HTTP UI service (`/`, `/ai/code`, `/health`).

## Quick start

```bash
tezz ai init-core
tezz ai learn-corpus --root examples --root lib --root TEZZAI --root tools
tezz ai train --v2
tezz ai code "build api websocket route with auth middleware" --code-only --v2
tezz ai serve --host 127.0.0.1 --port 8099 --v2
```

## Canonical paths

- Data: `TEZZAI/data/samples.tnxb`
- Model: `TEZZAI/data/model.taim`
- Codebook: `TEZZAI/data/codebook.tnxb`
- Web UI: `TEZZAI/web/`

