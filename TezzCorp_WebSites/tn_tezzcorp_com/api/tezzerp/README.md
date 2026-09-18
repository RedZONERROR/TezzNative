# TezzERP 3.0 Category APIs

This folder provides category-first API entry points for the non-modular TEZZ-ERP 3.0 pages.

- `dashboard/*`
- `profile/*`
- `students/*`
- `staff/*`
- `fees/*`
- `notices/*`
- `settings/*`
- `exams/*`
- `services/*`
- `billing/*`
- `license/*`
- `updates/*`

Current implementation is production-safe wrappers to stable `api/v2/*` handlers.
This allows page-wise migration without breaking existing data contracts.
