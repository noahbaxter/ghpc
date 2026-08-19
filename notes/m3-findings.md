# M3: getting the filesystem RPC to bind

Boot was stuck in a retry loop. Root cause was not a missing feature, it was
four separate mechanisms all rewriting game calls into runtime handlers purely
because the names matched.

## Symptom

`sceFsSifCallRpc` called SIF RPC, got a negative return, `DelayThread(1000)`,
retried forever. The debug panel showed `SID=0x00000000`, `server*=0x00000008`,
and only three RPC events ever captured: Init, Init, Call. No Bind.

## Why there was no bind

The bind lives inside the game's own `sceFsInit`. That function was being
replaced by a runtime stub, so the bind never ran, and `sceFsSifCallRpc` (which
was recompiled as real code) then called RPC against a client nobody connected.

## The four mechanisms

Our ELF is symbolized, so it offers 12,663 names to match against. GH2
statically links Sony's libraries, so those names are real functions with real
bodies, not SDK imports. Every layer that matches on name therefore misfires.

1. TOML `stubs` list, from the analyzer. Fixed by `config/stub-denylist.txt`.
2. Callsite relocation auto-bind in `ControlFlowEmitter::emitRelocationCallIfAvailable`.
   Rewrites `jal` targets into `ps2_stubs::X` when the relocation symbol matches
   a handler. Unconditional, no config.
3. `PS2Recompiler::hasResolvedStubHandler`, name match on the correctness-critical path.
4. `PS2Recompiler::isStubFunction`, which ends in a bare
   `ps2_runtime_calls::isStubName(function.name)`. This is the root: it drives
   `function.isStub`, which drives wrapper emission.

Removing entries from the TOML only addressed 1. Bodies stayed 14-line wrappers
until 4 was gated.

## Fix

`patches/0003-denylist-overrides-runtime-handler-binding.patch` adds a
`general.no_reloc_bind` config list and consults it in 2, 3, and 4, with the
check placed first in `isStubFunction` so it wins over address bindings and name
matches alike. `scripts/build.sh` writes the list into the TOML from
`config/stub-denylist.txt`, so it is reproducible rather than a hand edit.

Effect on the four denied functions:

| function | before | after |
|---|---|---|
| `sceFsInit` | 14 lines | 634 lines |
| `_sceFsSemInit` | 14 | 125 |
| `sceFsReset` | 14 | 90 |
| `_sceFsSigSema` | 14 | 37 |

## Result

The bind now happens:

```
before:  sid=0x00000000  (unbound)
after:   sid=0x80000001  rpc=0xff  send=8
         sid=0x80000001  rpc=0xc   send=39
```

`0x80000001` is the Sony fileio RPC service ID. The game is now making
well-formed calls to a service that does not exist on our side yet, instead of
flailing at an unbound client.

Observed payloads, to be checked against ps2sdk rather than inferred:

- `rpc=0xff`, 8 bytes: two guest pointers (0x005B5A40, 0x005B5E80). Looks like
  an init handshake exchanging buffer addresses.
- `rpc=0xc`, 39 bytes: 0x00000006, 0x01F7F000, 0x00000004, 0x01F7F0A0. The first
  dword was 0x03 in the broken run and is 0x06 now, so it is a command selector
  now being set from real state.

## Superseded

`patches/0002-implement-sceFs-sema-noops.patch` is deleted. It made the runtime's
`sceFs*` stubs return success, which papered over the problem and would have
masked the missing bind. The correct answer was to stop replacing the game's
code at all. Good argument for not having sent it anywhere.

## Note

Nothing renders yet. The screen is magenta, which is the runtime's
"GS never wrote a pixel" sentinel from `GenImageColor(..., MAGENTA)`.
