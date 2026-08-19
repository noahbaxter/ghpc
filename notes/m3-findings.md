# M3 binding the filesystem RPC

Boot was in a retry loop. Cause was four mechanisms rewriting game calls into
runtime handlers on name match.

## Symptom

`sceFsSifCallRpc` called SIF RPC, received a negative return, called
`DelayThread(1000)`, and retried. The debug panel showed `SID=0x00000000`,
`server*=0x00000008`, and three RPC events total: Init, Init, Call. No Bind.

## Cause

The bind is inside the game's `sceFsInit`, which was replaced by a runtime stub.
`sceFsSifCallRpc` was recompiled as real code and called RPC against an unbound
client.

## The four mechanisms

The ELF is symbolized and GH2 statically links Sony's libraries, so matched
names resolve to real function bodies rather than SDK imports.

1. TOML `stubs` list from the analyzer. Addressed by `config/stub-denylist.txt`.
2. Callsite relocation auto-bind in
   `ControlFlowEmitter::emitRelocationCallIfAvailable`, rewriting `jal` targets
   to `ps2_stubs::X` when the relocation symbol matches a handler. No config.
3. `PS2Recompiler::hasResolvedStubHandler`, name match on the
   correctness-critical path.
4. `PS2Recompiler::isStubFunction`, ending in
   `ps2_runtime_calls::isStubName(function.name)`. Drives `function.isStub`,
   which drives wrapper emission.

Removing TOML entries addressed only 1. Bodies remained 14-line wrappers until 4
was gated.

## Fix

`patches/0003-denylist-overrides-runtime-handler-binding.patch` adds a
`general.no_reloc_bind` config list, consulted in 2, 3 and 4, checked first in
`isStubFunction` so it precedes address bindings and name matches.
`scripts/build.sh` writes the list into the TOML from
`config/stub-denylist.txt`.

| function | before | after |
|---|---|---|
| `sceFsInit` | 14 lines | 634 lines |
| `_sceFsSemInit` | 14 | 125 |
| `sceFsReset` | 14 | 90 |
| `_sceFsSigSema` | 14 | 37 |

## Result

```
before:  sid=0x00000000  (unbound)
after:   sid=0x80000001  rpc=0xff  send=8
         sid=0x80000001  rpc=0xc   send=39
```

`0x80000001` is the Sony fileio RPC service ID. Calls are now well-formed
against a service not implemented on this side.

Observed payloads, unverified against ps2sdk:

- `rpc=0xff`, 8 bytes: two guest pointers (0x005B5A40, 0x005B5E80).
- `rpc=0xc`, 39 bytes: 0x00000006, 0x01F7F000, 0x00000004, 0x01F7F0A0. First
  dword was 0x03 in the unbound run.

## Superseded

`patches/0002-implement-sceFs-sema-noops.patch` removed. It made the runtime's
`sceFs*` stubs return success, which concealed the missing bind.

## State

Nothing renders. Framebuffer is magenta, the runtime's "GS never wrote a pixel"
sentinel from `GenImageColor(..., MAGENTA)`.
