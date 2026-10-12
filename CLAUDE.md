# CLAUDE.md

## Language

Everything in this project is done in English, even when the request is written in another language:

- Code, identifiers, and comments
- Docs (`README.md`, `TASKS.md`, this file) and site copy
- Commit messages and PR titles/descriptions

Conversation replies match the language of the request.

## Testing

Test-driven development:

- Tautological tests considered harmful.
- Change-detector tests considered harmful.
- Do not create regression tests for bug fixes without a genuine gap in behavior testing.

## Subagents

For an audit, a migration, or a review across a large codebase, delegate the work to subagents. When a subagent reports back, check its evidence before you accept it.

## Tasks

Keep a checklist in `TASKS.md`. Tick each item when it's done, and add anything new you find.
